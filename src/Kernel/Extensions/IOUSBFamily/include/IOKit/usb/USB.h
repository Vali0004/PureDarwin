/*
 * USB.h - PureDarwin minimal replacement.
 *
 * The real SDK USB.h (from a much newer macOS release than this kext build
 * targets) gates every descriptor/request struct definition behind
 * `#if TARGET_OS_OSX || TARGET_OS_MACCATALYST`, which never activates in
 * this kernel-private cross-compiled kext build (no TargetConditionals.h
 * feature detection wired up the way a normal app build has it) - the
 * whole header compiled down to nothing. Rather than chase that, this is a
 * small self-contained header with exactly the standard-USB-spec structs
 * and constants IOUSBFamily/RavynXHCIPort actually use. Field layouts are
 * the literal USB spec wire format, not something Apple-specific, so
 * there's no fidelity loss versus the real header for what we use.
 */

#ifndef _IOKIT_USB_USB_H
#define _IOKIT_USB_USB_H

#include <IOKit/usb/USBSpec.h>
#include <IOKit/IOTypes.h>

typedef UInt16 USBDeviceAddress;
typedef UInt16 USBStatus;

#pragma pack(push, 1)

typedef struct {
    UInt8  bLength;
    UInt8  bDescriptorType;
} IOUSBDescriptorHeader;

typedef struct {
    UInt8  bLength;
    UInt8  bDescriptorType;
    UInt16 bcdUSB;
    UInt8  bDeviceClass;
    UInt8  bDeviceSubClass;
    UInt8  bDeviceProtocol;
    UInt8  bMaxPacketSize0;
    UInt16 idVendor;
    UInt16 idProduct;
    UInt16 bcdDevice;
    UInt8  iManufacturer;
    UInt8  iProduct;
    UInt8  iSerialNumber;
    UInt8  bNumConfigurations;
} IOUSBDeviceDescriptor;

typedef struct {
    UInt8  bLength;
    UInt8  bDescriptorType;
    UInt16 wTotalLength;
    UInt8  bNumInterfaces;
    UInt8  bConfigurationValue;
    UInt8  iConfiguration;
    UInt8  bmAttributes;
    UInt8  MaxPower;
} IOUSBConfigurationDescriptor;

typedef struct {
    UInt8  bLength;
    UInt8  bDescriptorType;
    UInt8  bInterfaceNumber;
    UInt8  bAlternateSetting;
    UInt8  bNumEndpoints;
    UInt8  bInterfaceClass;
    UInt8  bInterfaceSubClass;
    UInt8  bInterfaceProtocol;
    UInt8  iInterface;
} IOUSBInterfaceDescriptor;

typedef struct {
    UInt8  bLength;
    UInt8  bDescriptorType;
    UInt8  bEndpointAddress;
    UInt8  bmAttributes;
    UInt16 wMaxPacketSize;
    UInt8  bInterval;
} IOUSBEndpointDescriptor;

typedef struct {
    UInt8  bLength;
    UInt8  bDescriptorType;
    UInt16 bString[1];
} IOUSBStringDescriptor;

/* Endpoint companion descriptor, referenced by IOUSBInterface.h's
 * CalculateFullMaxPacketSize signature - full SuperSpeed fields not needed
 * for anything we implement. */
typedef struct {
    UInt8  bLength;
    UInt8  bDescriptorType;
    UInt8  bMaxBurst;
    UInt8  bmAttributes;
    UInt16 wBytesPerInterval;
} IOUSBSuperSpeedEndpointCompanionDescriptor;

typedef struct {
    UInt8  bmRequestType;
    UInt8  bRequest;
    UInt16 wValue;
    UInt16 wIndex;
    UInt16 wLength;
    void  *pData;
    UInt32 wLenDone;
} IOUSBDevRequest;

typedef struct {
    UInt8  bmRequestType;
    UInt8  bRequest;
    UInt16 wValue;
    UInt16 wIndex;
    UInt16 wLength;
    class IOMemoryDescriptor *pData;
    UInt32 wLenDone;
} IOUSBDevRequestDesc;

typedef void (*IOUSBCompletionAction)(void *target, void *parameter, IOReturn status, UInt32 bufferSizeRemaining);

typedef struct IOUSBCompletion {
    void *target;
    IOUSBCompletionAction action;
    void *parameter;
} IOUSBCompletion;

/* Isoc/low-latency-isoc/timestamped completions: referenced in IOUSBPipe.h/
 * IOUSBControllerListElement.h/IOUSBControllerV2.h signatures we don't
 * actually call (no isoc support in any UIM we have) - defined only so
 * those headers parse. */
typedef void (*IOUSBCompletionActionWithTimeStamp)(void *target, void *parameter, IOReturn status,
                                                     UInt32 bufferSizeRemaining, UInt64 timeStamp);
typedef struct IOUSBCompletionWithTimeStamp {
    void *target;
    IOUSBCompletionActionWithTimeStamp action;
    void *parameter;
} IOUSBCompletionWithTimeStamp;

typedef struct {
    UInt16   frStatus;
    UInt16   frReqCount;
    UInt16   frActCount;
} IOUSBIsocFrame;

typedef struct {
    UInt64   frTimeStamp;
    UInt16   frStatus;
    UInt16   frReqCount;
    UInt16   frActCount;
} IOUSBLowLatencyIsocFrame;

typedef IOUSBCompletion IOUSBIsocCompletion;
typedef IOUSBCompletion IOUSBLowLatencyIsocCompletion;

typedef struct {
    UInt16 bInterfaceClass;
    UInt16 bInterfaceSubClass;
    UInt16 bInterfaceProtocol;
    UInt16 bAlternateSetting;
} IOUSBFindInterfaceRequest;

typedef struct {
    UInt8  type;
    UInt8  direction;
    UInt16 maxPacketSize;
    UInt8  interval;
} IOUSBFindEndpointRequest;

#pragma pack(pop)

enum {
    kIOUSBFindInterfaceDontCare = 0xFFFF
};

#ifndef kIOUSBInterfaceNotFound
#define kIOUSBInterfaceNotFound iokit_usb_err(0x4e)
#endif
#ifndef iokit_usb_err
#define iokit_usb_err(x) (0xe0004000 | (x))
#endif

enum {
    kUSBMaxPipes = 32
};

#endif /* !_IOKIT_USB_USB_H */
