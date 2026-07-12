#include <IOKit/IOLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include <IOKit/hidsystem/IOHIDShared.h>
#include <kern/thread.h>
#include "RavynXHCIKeyboard.h"
#include "RavynXHCIPort.h"
#include "../ApplePS2Controller/ApplePS2KeyboardMap.h"

#define super IOHIKeyboard
OSDefineMetaClassAndStructors(RavynXHCIKeyboard, IOHIKeyboard);

#define DEADKEY 0xFF

static const UInt8 gUSBToADB[256] = {
    DEADKEY, DEADKEY, DEADKEY, DEADKEY,
    0x00, 0x0b, 0x08, 0x02, 0x0e, 0x03, 0x05, 0x04, 0x22, 0x26, 0x28, 0x25,
    0x2e, 0x2d, 0x1f, 0x23, 0x0c, 0x0f, 0x01, 0x11, 0x20, 0x09, 0x0d, 0x07,
    0x10, 0x06, 0x12, 0x13, 0x14, 0x15, 0x17, 0x16, 0x1a, 0x1c, 0x19, 0x1d,
    0x24, 0x35, 0x33, 0x30, 0x31, 0x1b, 0x18, 0x21, 0x1e, 0x2a, 0x2a, 0x29,
    0x27, 0x32, 0x2b, 0x2f, 0x2c, 0x39, 0x7a, 0x78, 0x63, 0x76, 0x60, 0x61,
    0x62, 0x64, 0x65, 0x6d, 0x67, 0x6f, 0x69, 0x6b, 0x71, 0x72, 0x73, 0x74,
    0x75, 0x77, 0x79, 0x7c, 0x7b, 0x7d, 0x7e, 0x47, 0x4b, 0x43, 0x4e, 0x45,
    0x4c, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5b, 0x5c, 0x52, 0x41,
    0x0a, 0x6e, 0x7f, 0x51, 0x69, 0x6b, 0x71, 0x6a, 0x40, 0x4f, 0x50, 0x5a,
    DEADKEY, DEADKEY, DEADKEY, DEADKEY, DEADKEY, 0x72,
};

static const UInt8 gUSBModToADB[8] = {
    0x3b, /* left control */
    0x38, /* left shift */
    0x3a, /* left option */
    0x37, /* left command */
    0x3e, /* right control */
    0x3c, /* right shift */
    0x3d, /* right option */
    0x36  /* right command */
};

bool RavynXHCIKeyboard::initWithPort(RavynXHCIPort * port, int kbdIndex)
{
    if (!super::init()) return false;
    _port = port;
    _kbdIndex = kbdIndex;
    _running = false;
    bzero(_lastReport, sizeof(_lastReport));
    return true;
}

bool RavynXHCIKeyboard::start(IOService * provider)
{
    if (!super::start(provider)) return false;
    _running = true;

    thread_t thread = THREAD_NULL;
    if (kernel_thread_start((thread_continue_t)&RavynXHCIKeyboard::pollThread, this, &thread) != KERN_SUCCESS) {
        IOLog("RavynXHCIKeyboard: failed to start poll thread\n");
        _running = false;
        return false;
    }
    thread_deallocate(thread);
    IOLog("RavynXHCIKeyboard: started usbkbd%d\n", _kbdIndex);
    return true;
}

void RavynXHCIKeyboard::stop(IOService * provider)
{
    _running = false;
    super::stop(provider);
}

void RavynXHCIKeyboard::free()
{
    _running = false;
    super::free();
}

void RavynXHCIKeyboard::pollThread(void * arg, wait_result_t)
{
    RavynXHCIKeyboard * self = (RavynXHCIKeyboard *)arg;
    self->pollLoop();
    thread_terminate(current_thread());
}

void RavynXHCIKeyboard::pollLoop()
{
    while (_running) {
        UInt8 report[8];
        bzero(report, sizeof(report));
        if (_port && _port->pollKeyboard(_kbdIndex, report, 250)) {
            handleReport(report);
        } else {
            IOSleep(10);
        }
    }
}

static bool reportHasUsage(const UInt8 report[8], UInt8 usage)
{
    for (int i = 2; i < 8; i++) {
        if (report[i] == usage) return true;
    }
    return false;
}

void RavynXHCIKeyboard::handleReport(const UInt8 report[8])
{
    static bool loggedFirstReport = false;
    if (!loggedFirstReport) {
        IOLog("RavynXHCIKeyboard: first report mods=%02x keys=%02x %02x %02x %02x %02x %02x\n",
            report[0], report[2], report[3], report[4], report[5], report[6], report[7]);
        loggedFirstReport = true;
    }

    if (report[2] == 1) {
        IOLog("RavynXHCIKeyboard: rollover report ignored\n");
        bcopy(report, _lastReport, sizeof(_lastReport));
        return;
    }

    UInt8 changedMods = report[0] ^ _lastReport[0];
    for (UInt8 bit = 0; bit < 8; bit++) {
        if (changedMods & (1U << bit)) {
            dispatchModifier(bit, !!(report[0] & (1U << bit)));
        }
    }

    for (int i = 2; i < 8; i++) {
        UInt8 usage = _lastReport[i];
        if (usage >= 4 && !reportHasUsage(report, usage)) {
            dispatchUSBUsage(usage, false);
        }
    }
    for (int i = 2; i < 8; i++) {
        UInt8 usage = report[i];
        if (usage >= 4 && !reportHasUsage(_lastReport, usage)) {
            dispatchUSBUsage(usage, true);
        }
    }

    bcopy(report, _lastReport, sizeof(_lastReport));
}

void RavynXHCIKeyboard::dispatchUSBUsage(UInt8 usage, bool down)
{
    if (usage > 0x75) return;

    UInt8 adb = gUSBToADB[usage];
    if (adb == DEADKEY) return;

    AbsoluteTime now;
    clock_get_uptime((uint64_t *)&now);
    dispatchKeyboardEvent(adb, down, now);
}

void RavynXHCIKeyboard::dispatchModifier(UInt8 bit, bool down)
{
    if (bit >= 8) return;

    AbsoluteTime now;
    clock_get_uptime((uint64_t *)&now);
    dispatchKeyboardEvent(gUSBModToADB[bit], down, now);
}

void RavynXHCIKeyboard::setAlphaLockFeedback(bool)
{
    /* LED output reports can come later; input is enough for boot console. */
}

const unsigned char * RavynXHCIKeyboard::defaultKeymapOfLength(UInt32 * length)
{
    *length = sizeof(applePS2USAKeyMap);
    return applePS2USAKeyMap;
}

UInt32 RavynXHCIKeyboard::maxKeyCodes()
{
    return NX_NUMKEYCODES;
}

UInt32 RavynXHCIKeyboard::deviceType()
{
    return NX_EVS_DEVICE_TYPE_KEYBOARD;
}

UInt32 RavynXHCIKeyboard::interfaceID()
{
    return NX_EVS_DEVICE_INTERFACE_ADB;
}
