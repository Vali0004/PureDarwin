#include <IOKit/IOLib.h>
#include <IOKit/hidsystem/IOHIDParameter.h>
#include <IOKit/hidsystem/IOHIDShared.h>
#include <kern/thread.h>
#include <kern/thread_call.h>
#include <miscfs/devfs/devfs.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include "RavynXHCIKeyboard.h"
#include "RavynXHCIPort.h"
#include "../ApplePS2Controller/ApplePS2KeyboardMap.h"

#define super IOHIKeyboard
OSDefineMetaClassAndStructors(RavynXHCIKeyboard, IOHIKeyboard);

#define DEADKEY 0xFF

enum {
    kUSBHIDInterfaceClass       = 3,
    kUSBBootInterfaceSubClass   = 1,
    kUSBKeyboardInterfaceProtocol = 1
};

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

/* ------------------------------------------------------------------------
 * /dev/xhci_kbd character device.
 *
 * The keyboard already feeds the in-kernel IOHIDSystem via IOHIKeyboard, but
 * a userspace X server has no way to read that. Mirror the mouse's cdev: a
 * small ring buffer of raw key events (USB HID usage + up/down) exposed as a
 * non-blocking char device that the Xorg input driver polls.
 * ------------------------------------------------------------------------ */

extern "C" int devfs_is_ready(void);

struct XHCIKbdEvent {
    UInt32 sequence;
    UInt8  usage;       /* USB HID usage (regular 0x04..0x75, mods 0xE0..0xE7) */
    UInt8  down;        /* 1 = pressed, 0 = released */
    UInt8  reserved[2];
};

static void *gKbdNode;
static int gKbdMajor = -1;
static thread_call_t gKbdRetryCall;
static IONotifier *gKbdBSDNotifier;
static IOLock *gKbdLock;
static UInt32 gKbdSequence;
static UInt32 gKbdReadIndex;
static UInt32 gKbdWriteIndex;
static XHCIKbdEvent gKbdEvents[64];

static void xhci_kbd_publish_retry(thread_call_param_t, thread_call_param_t);
static bool xhci_kbd_iobsd_published(void *, void *, IOService *, IONotifier *);

static int xhci_kbd_open(dev_t, int, int, struct proc *) { return 0; }
static int xhci_kbd_close(dev_t, int, int, struct proc *) { return 0; }

static int
xhci_kbd_read(dev_t, struct uio *uio, int)
{
    XHCIKbdEvent event;
    int error = 0;

    if (!gKbdLock)
        return ENXIO;
    if (uio_resid(uio) < (user_ssize_t)sizeof(event))
        return EINVAL;

    IOLockLock(gKbdLock);
    if (gKbdReadIndex == gKbdWriteIndex) {
        error = EAGAIN;
    } else {
        event = gKbdEvents[gKbdReadIndex % (UInt32)(sizeof(gKbdEvents) / sizeof(gKbdEvents[0]))];
        gKbdReadIndex++;
    }
    IOLockUnlock(gKbdLock);

    if (error)
        return error;
    return uiomove((const char *)&event, (int)sizeof(event), uio);
}

static struct cdevsw xhci_kbd_cdevsw =
{
    /* d_open     */ xhci_kbd_open,
    /* d_close    */ xhci_kbd_close,
    /* d_read     */ xhci_kbd_read,
    /* d_write    */ eno_rdwrt,
    /* d_ioctl    */ eno_ioctl,
    /* d_stop     */ eno_stop,
    /* d_reset    */ eno_reset,
    /* d_ttys     */ 0,
    /* d_select   */ eno_select,
    /* d_mmap     */ eno_mmap,
    /* d_strategy */ eno_strat,
    /* d_getc     */ eno_getc,
    /* d_putc     */ eno_putc,
    /* d_type     */ 0
};

static void
xhci_kbd_schedule_retry()
{
    AbsoluteTime deadline;

    if (gKbdNode)
        return;
    if (!gKbdRetryCall) {
        gKbdRetryCall = thread_call_allocate(xhci_kbd_publish_retry, NULL);
        if (!gKbdRetryCall)
            return;
    }
    clock_interval_to_deadline(1, kSecondScale, &deadline);
    thread_call_enter_delayed(gKbdRetryCall, deadline);
}

static void
xhci_kbd_publish()
{
    if (gKbdNode)
        return;
    if (!gKbdLock) {
        gKbdLock = IOLockAlloc();
        if (!gKbdLock)
            return;
    }
    if (gKbdMajor < 0) {
        gKbdMajor = cdevsw_add(-1, &xhci_kbd_cdevsw);
        if (gKbdMajor < 0)
            return;
    }

    if (!devfs_is_ready()) {
        if (!gKbdBSDNotifier) {
            OSDictionary *matching = IOService::resourceMatching("IOBSD");
            if (matching) {
                gKbdBSDNotifier = IOService::addMatchingNotification(gIOPublishNotification,
                                                                     matching,
                                                                     xhci_kbd_iobsd_published,
                                                                     NULL, NULL);
                matching->release();
            }
        }
        xhci_kbd_schedule_retry();
        return;
    }

    gKbdNode = devfs_make_node(makedev(gKbdMajor, 0), DEVFS_CHAR,
                               0, 0, 0666, "xhci_kbd");
    if (!gKbdNode)
        xhci_kbd_schedule_retry();
    else
        IOLog("RavynXHCIKeyboard: published /dev/xhci_kbd\n");
}

static void
xhci_kbd_publish_retry(thread_call_param_t, thread_call_param_t)
{
    xhci_kbd_publish();
}

static bool
xhci_kbd_iobsd_published(void *, void *, IOService *, IONotifier *notifier)
{
    xhci_kbd_publish();
    if (gKbdNode && notifier) {
        notifier->remove();
        if (gKbdBSDNotifier == notifier)
            gKbdBSDNotifier = NULL;
    }
    return true;
}

static void
xhci_kbd_push_event(UInt8 usage, bool down)
{
    UInt32 slot;
    XHCIKbdEvent event;

    if (!gKbdLock) {
        xhci_kbd_publish();
        if (!gKbdLock)
            return;
    }

    event.sequence = ++gKbdSequence;
    event.usage = usage;
    event.down = down ? 1 : 0;
    event.reserved[0] = event.reserved[1] = 0;

    IOLockLock(gKbdLock);
    slot = gKbdWriteIndex % (UInt32)(sizeof(gKbdEvents) / sizeof(gKbdEvents[0]));
    gKbdEvents[slot] = event;
    gKbdWriteIndex++;
    if (gKbdWriteIndex - gKbdReadIndex > (UInt32)(sizeof(gKbdEvents) / sizeof(gKbdEvents[0])))
        gKbdReadIndex = gKbdWriteIndex - (UInt32)(sizeof(gKbdEvents) / sizeof(gKbdEvents[0]));
    IOLockUnlock(gKbdLock);
}

bool RavynXHCIKeyboard::initWithPort(RavynXHCIPort * port, int kbdIndex)
{
    if (!super::init()) return false;
    _port = port;
    _kbdIndex = kbdIndex;
    _running = false;
    bzero(_lastReport, sizeof(_lastReport));

    setName("RavynXHCIKeyboard");
    setProperty("HIDKeyboardKeysDefined", kOSBooleanTrue);
    setProperty(kIOHIDVirtualHIDevice, kOSBooleanFalse);
    setProperty(kIOHIDKindKey, kHIKeyboardDevice, 32);
    setProperty(kIOHIDInterfaceIDKey, NX_EVS_DEVICE_INTERFACE_ADB, 32);
    setProperty(kIOHIDSubinterfaceIDKey, NX_EVS_DEVICE_TYPE_KEYBOARD, 32);
    setProperty("Transport", "USB");
    setProperty("USB Product Name", "Ravyn USB Keyboard");
    setProperty("bInterfaceClass", kUSBHIDInterfaceClass, 8);
    setProperty("bInterfaceSubClass", kUSBBootInterfaceSubClass, 8);
    setProperty("bInterfaceProtocol", kUSBKeyboardInterfaceProtocol, 8);
    return true;
}

bool RavynXHCIKeyboard::start(IOService * provider)
{
    if (!super::start(provider)) return false;
    _running = true;

    xhci_kbd_publish();

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

    xhci_kbd_push_event(usage, down);

    UInt8 adb = gUSBToADB[usage];
    if (adb == DEADKEY) return;

    AbsoluteTime now;
    clock_get_uptime((uint64_t *)&now);
    dispatchKeyboardEvent(adb, down, now);
}

void RavynXHCIKeyboard::dispatchModifier(UInt8 bit, bool down)
{
    if (bit >= 8) return;

    xhci_kbd_push_event((UInt8)(0xE0 + bit), down);

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
