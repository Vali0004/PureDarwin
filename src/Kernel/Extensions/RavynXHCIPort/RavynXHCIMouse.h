#ifndef _RAVYN_XHCI_MOUSE_H
#define _RAVYN_XHCI_MOUSE_H

#include <IOKit/IOService.h>

class RavynXHCIPort;

/* HID boot mouse driver. For now this owns enumeration/polling only; pointer
 * delivery should be wired through a small native path rather than importing
 * IOHIDSystem's full legacy pointing stack. */
class RavynXHCIMouse : public IOService
{
    OSDeclareDefaultStructors(RavynXHCIMouse);

private:
    RavynXHCIPort        * _port;
    int                    _mouseIndex;
    bool                   _running;
    UInt8                  _lastButtons;

    static void pollThread(void * arg, wait_result_t);
    void pollLoop();
    void handleReport(const UInt8 report[8]);

public:
    bool initWithPort(RavynXHCIPort * port, int mouseIndex);
    virtual bool start(IOService * provider) APPLE_KEXT_OVERRIDE;
    virtual void stop(IOService * provider) APPLE_KEXT_OVERRIDE;
    virtual void free() APPLE_KEXT_OVERRIDE;
};

#endif /* _RAVYN_XHCI_MOUSE_H */
