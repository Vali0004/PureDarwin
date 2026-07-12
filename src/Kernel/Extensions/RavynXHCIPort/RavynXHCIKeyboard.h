#ifndef _RAVYN_XHCI_KEYBOARD_H
#define _RAVYN_XHCI_KEYBOARD_H

#include <IOKit/hidsystem/IOHIKeyboard.h>

class RavynXHCIPort;

class RavynXHCIKeyboard : public IOHIKeyboard
{
    OSDeclareDefaultStructors(RavynXHCIKeyboard);

private:
    RavynXHCIPort * _port;
    int             _kbdIndex;
    bool            _running;
    UInt8           _lastReport[8];

    static void pollThread(void * arg, wait_result_t);
    void pollLoop();
    void handleReport(const UInt8 report[8]);
    void dispatchUSBUsage(UInt8 usage, bool down);
    void dispatchModifier(UInt8 bit, bool down);

public:
    bool initWithPort(RavynXHCIPort * port, int kbdIndex);
    virtual bool start(IOService * provider) APPLE_KEXT_OVERRIDE;
    virtual void stop(IOService * provider) APPLE_KEXT_OVERRIDE;
    virtual void free() APPLE_KEXT_OVERRIDE;

    virtual const unsigned char * defaultKeymapOfLength(UInt32 * length) APPLE_KEXT_OVERRIDE;
    virtual void setAlphaLockFeedback(bool locked) APPLE_KEXT_OVERRIDE;
    virtual UInt32 maxKeyCodes() APPLE_KEXT_OVERRIDE;
    virtual UInt32 deviceType() APPLE_KEXT_OVERRIDE;
    virtual UInt32 interfaceID() APPLE_KEXT_OVERRIDE;
};

#endif /* _RAVYN_XHCI_KEYBOARD_H */
