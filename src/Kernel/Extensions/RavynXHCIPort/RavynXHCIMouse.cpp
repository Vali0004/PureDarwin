#include <IOKit/IOLib.h>
#include <kern/thread.h>
#include "RavynXHCIMouse.h"
#include "RavynXHCIPort.h"

#define super IOService
OSDefineMetaClassAndStructors(RavynXHCIMouse, IOService);

bool RavynXHCIMouse::initWithPort(RavynXHCIPort *port, int mouseIndex)
{
    if (!super::init()) return false;
    _port = port;
    _mouseIndex = mouseIndex;
    _running = false;
    _lastButtons = 0;
    setName("RavynXHCIMouse");
    return true;
}

bool RavynXHCIMouse::start(IOService *provider)
{
    if (!super::start(provider)) return false;

    _running = true;
    thread_t thread = THREAD_NULL;
    if (kernel_thread_start((thread_continue_t)&RavynXHCIMouse::pollThread, this, &thread) != KERN_SUCCESS) {
        IOLog("RavynXHCIMouse: failed to start poll thread\n");
        _running = false;
        return false;
    }
    thread_deallocate(thread);
    IOLog("RavynXHCIMouse: started usbmouse%d\n", _mouseIndex);
    return true;
}

void RavynXHCIMouse::stop(IOService *provider)
{
    _running = false;
    super::stop(provider);
}

void RavynXHCIMouse::free()
{
    _running = false;
    super::free();
}

void RavynXHCIMouse::pollThread(void *arg, wait_result_t)
{
    RavynXHCIMouse *self = (RavynXHCIMouse *)arg;
    self->pollLoop();
    thread_terminate(current_thread());
}

void RavynXHCIMouse::pollLoop()
{
    while (_running) {
        UInt8 report[8];
        bzero(report, sizeof(report));
        if (_port && _port->pollMouse(_mouseIndex, report, 250)) {
            handleReport(report);
        } else {
            IOSleep(10);
        }
    }
}

void RavynXHCIMouse::handleReport(const UInt8 report[8])
{
    /* Standard HID boot mouse report: byte0 = button bitmap (bit0=left,
     * bit1=right, bit2=middle), byte1 = dx (signed), byte2 = dy (signed),
     * byte3 = wheel (signed, optional - not all boot mice report it). */
    UInt8 buttons = report[0];
    SInt8 dx = (SInt8)report[1];
    SInt8 dy = (SInt8)report[2];
    SInt8 wheel = (SInt8)report[3];

    if (buttons != _lastButtons || dx || dy || wheel) {
        IOLog("RavynXHCIMouse: report buttons=%02x dx=%d dy=%d wheel=%d\n",
              buttons, dx, dy, wheel);
        _lastButtons = buttons;
    }
}
