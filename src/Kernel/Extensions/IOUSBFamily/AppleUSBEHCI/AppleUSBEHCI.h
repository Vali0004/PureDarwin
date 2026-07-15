#ifndef _IOKIT_AppleUSBEHCI_H
#define _IOKIT_AppleUSBEHCI_H

#include <IOKit/IOService.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/usb/IOUSBCommand.h>
#include <IOKit/usb/IOUSBControllerV3.h>

#include "USBEHCI.h"

typedef struct EHCIGeneralTransferDescriptor
    *EHCIGeneralTransferDescriptorPtr;
typedef struct EHCIIsochTransferDescriptor
    *EHCIIsochTransferDescriptorPtr;
typedef struct EHCISplitIsochTransferDescriptor
    *EHCISplitIsochTransferDescriptorPtr;

struct EHCIGeneralTransferDescriptor
{
    EHCIGeneralTransferDescriptorSharedPtr pShared;
    IOPhysicalAddress pPhysical;
    EHCIGeneralTransferDescriptorPtr pLogicalNext;
    IOUSBCommand *command;
};

struct EHCIIsochTransferDescriptor
{
    EHCIIsochTransferDescriptorSharedPtr pShared;
    IOPhysicalAddress pPhysical;
};

struct EHCISplitIsochTransferDescriptor
{
    EHCISplitIsochTransferDescriptorSharedPtr pShared;
    IOPhysicalAddress pPhysical;
};

class AppleUSBEHCI : public IOUSBControllerV3
{
    OSDeclareDefaultStructors(AppleUSBEHCI)

public:
    virtual bool init(OSDictionary *propTable);
    virtual bool start(IOService *provider);
    virtual void stop(IOService *provider);
    virtual void free();

    virtual IOReturn UIMOpenPipe(USBDeviceAddress address, UInt8 speed, Endpoint *endpoint);
    virtual IOReturn UIMClosePipe(USBDeviceAddress address, Endpoint *endpoint);
    virtual IOReturn UIMAbortPipe(USBDeviceAddress address, Endpoint *endpoint);
    virtual IOReturn UIMClearPipeStall(USBDeviceAddress address, Endpoint *endpoint);
    virtual IOReturn UIMDeviceRequest(IOUSBDevRequest *request, USBDeviceAddress address);
    virtual IOReturn UIMReadWrite(IOMemoryDescriptor *buffer, USBDeviceAddress address,
                                  Endpoint *endpoint, bool isWrite);

    virtual IOReturn UIMInitialize(IOService *provider);
    virtual IOReturn UIMFinalize();
    virtual IOReturn UIMCreateControlEndpoint(UInt8 functionNumber,
                                             UInt8 endpointNumber,
                                             UInt16 maxPacketSize,
                                             UInt8 speed,
                                             USBDeviceAddress highSpeedHub,
                                             int highSpeedPort);
    virtual IOReturn UIMCreateBulkEndpoint(UInt8 functionNumber,
                                          UInt8 endpointNumber,
                                          UInt8 direction,
                                          UInt8 speed,
                                          UInt16 maxPacketSize,
                                          USBDeviceAddress highSpeedHub,
                                          int highSpeedPort);
    virtual IOReturn UIMCreateInterruptEndpoint(short functionAddress,
                                               short endpointNumber,
                                               UInt8 direction,
                                               short speed,
                                               UInt16 maxPacketSize,
                                               short pollingRate,
                                               USBDeviceAddress highSpeedHub,
                                               int highSpeedPort);
    virtual IOReturn UIMCreateIsochEndpoint(short functionAddress,
                                           short endpointNumber,
                                           UInt32 maxPacketSize,
                                           UInt8 direction,
                                           USBDeviceAddress highSpeedHub,
                                           int highSpeedPort);
    virtual UInt32 GetBandwidthAvailable();
    virtual UInt64 GetFrameNumber();
    virtual UInt32 GetFrameNumber32();
    virtual IOReturn GetFrameNumberWithTime(UInt64 *frameNumber, AbsoluteTime *theTime);
    static IOReturn GatedGetFrameNumberWithTime(OSObject *owner, void *arg0,
                                                void *arg1, void *arg2, void *arg3);
    virtual IODMACommand *GetNewDMACommand();

    virtual IOReturn ResetControllerState();
    virtual IOReturn RestartControllerFromReset();
    virtual IOReturn SaveControllerStateForSleep();
    virtual IOReturn RestoreControllerStateFromSleep();
    virtual IOReturn DozeController();
    virtual IOReturn WakeControllerFromDoze();
    virtual IOReturn UIMEnableAddressEndpoints(USBDeviceAddress address, bool enable);
    virtual IOReturn UIMEnableAllEndpoints(bool enable);
    virtual IOReturn EnableInterruptsFromController(bool enable);

    IOReturn DeallocateITD(EHCIIsochTransferDescriptorPtr descriptor);
    IOReturn DeallocateSITD(EHCISplitIsochTransferDescriptorPtr descriptor);

protected:
    IOPCIDevice *_device;
    IOMemoryDescriptor *_barDesc;
    IOMemoryMap *_deviceBase;
    volatile UInt8 *_capRegs;
    volatile UInt8 *_opRegs;
    UInt64 _frameNumber;
    UInt32 _bandwidthAvailable;
    UInt8 _capLength;
    UInt8 _rootHubPorts;
    bool _uimInitialized;
    IOBufferMemoryDescriptor *_asyncQHMem;
    EHCIAsyncQueueHeadPtr _asyncQH;
    USBPhysicalAddress32 _asyncQHPhys;
    IOBufferMemoryDescriptor *_qTDMem;
    EHCIGeneralTransferDescriptorSharedPtr _qTDPool;
    USBPhysicalAddress32 _qTDPoolPhys;

    UInt8 capRead8(UInt32 offset);
    UInt16 capRead16(UInt32 offset);
    UInt32 capRead32(UInt32 offset);
    UInt32 opRead32(UInt32 offset);
    void opWrite32(UInt32 offset, UInt32 value);
    bool mapEHCIRegisters(IOPCIDevice *provider);
    void claimBIOSOwnership(void);
    bool haltController(void);
    bool resetController(void);
    bool setupAsyncSchedule(void);
    bool runController(bool run);
    bool enableAsyncSchedule(bool enable);
    EHCIGeneralTransferDescriptorSharedPtr qTDAt(UInt32 index);
    USBPhysicalAddress32 qTDPhys(UInt32 index);
    void setQTDBuffer(EHCIGeneralTransferDescriptorSharedPtr qtd,
                      USBPhysicalAddress32 phys, UInt32 length);
    void fillQTD(EHCIGeneralTransferDescriptorSharedPtr qtd,
                 USBPhysicalAddress32 next, UInt32 pid, UInt32 dataToggle,
                 USBPhysicalAddress32 buffer, UInt32 length, bool interruptOnComplete);
    IOReturn waitForQTD(EHCIGeneralTransferDescriptorSharedPtr qtd,
                        UInt32 timeoutMs);
    IOReturn controlTransfer(USBDeviceAddress address, IOUSBDevRequest *request);
    void releaseAsyncSchedule(void);
    void releaseHardwareResources(void);
};

#endif
