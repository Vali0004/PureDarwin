#include <IOKit/IOLib.h>
#include <IOKit/IOCommandGate.h>
#include <IOKit/IODMACommand.h>
#include "AppleUSBOHCI.h"

#define super IOUSBControllerV3
OSDefineMetaClassAndStructors(AppleUSBOHCI, IOUSBControllerV3)

bool AppleUSBOHCI::init(OSDictionary *propTable)
{
    if (!super::init(propTable))
        return false;

    _deviceBase = NULL;
    _pendingHead = _pendingTail = NULL;
    _vendorID = _deviceID = _revisionID = 0;
    _pOHCIRegisters = NULL;
    _pHCCA = NULL;
    _hccaBuffer = NULL;
    _pIsochHead = _pIsochTail = NULL;
    _pBulkHead = _pBulkTail = NULL;
    _pControlHead = _pControlTail = NULL;
    _pFreeTD = _pLastFreeTD = _pPendingTD = NULL;
    _pFreeITD = _pLastFreeITD = NULL;
    _pFreeED = _pLastFreeED = NULL;
    _edMBHead = NULL;
    _gtdMBHead = NULL;
    _itdMBHead = NULL;
    _frameNumber = 0;
    _rootHubFuncAddress = 0;
    _OptiOn = 0;
    _isochBandwidthAvail = 0;
    _disablePortsBitmap = 0;
    _dataAllocationSize = 0;
    _filterInterruptSource = NULL;
    _uimInitialized = false;
    _hccaPhysAddr = 0;
    _lowLatencyIsochTDsProcessed = 0;
    _filterInterruptCount = 0;
    _framesUpdated = 0;
    _framesError = 0;
    _resumeDetectedInterrupt = 0;
    _unrecoverableErrorInterrupt = 0;
    _rootHubStatusChangeInterrupt = 0;
    _writeDoneHeadInterrupt = 0;
    _frameNumberOverflowInterrupt = 0;
    _savedDoneQueueHead = 0;
    _producerCount = 0;
    _consumerCount = 0;
    _wdhLock = IOSimpleLockAlloc();
    _timeElapsed = 0;
    _tempAnchorFrame = _anchorFrame = 0;
    _ExpressCardPort = 0;
    _badExpressCardAttached = false;
    _needToReEnableRHSCInterrupt = false;
    _rootHubStatuschangedInterruptReceived = false;
    _controllerAvailable = false;
    _ohciErrata64Bits = 0;
    _usb_remote_wakeup = NULL;
    _remote_wakeup_occurred = false;
    bzero(&_errors, sizeof(_errors));
    bzero(_pInterruptHead, sizeof(_pInterruptHead));
    bzero(_savedHcRhPortStatus, sizeof(_savedHcRhPortStatus));
    return _wdhLock != NULL;
}

bool AppleUSBOHCI::start(IOService *provider)
{
    if (!super::start(provider))
        return false;

    IOLog("AppleUSBOHCI: controller lifecycle scaffold present; hardware init disabled\n");
    return false;
}

void AppleUSBOHCI::stop(IOService *provider)
{
    UIMFinalize();
    super::stop(provider);
}

bool AppleUSBOHCI::finalize(IOOptionBits options)
{
    return super::finalize(options);
}

IOReturn AppleUSBOHCI::message(UInt32 type, IOService *provider, void *argument)
{
    return super::message(type, provider, argument);
}

void AppleUSBOHCI::powerChangeDone(unsigned long)
{
}

bool AppleUSBOHCI::willTerminate(IOService *provider, IOOptionBits options)
{
    return super::willTerminate(provider, options);
}

bool AppleUSBOHCI::didTerminate(IOService *provider, IOOptionBits options, bool *defer)
{
    return super::didTerminate(provider, options, defer);
}

void AppleUSBOHCI::free()
{
    if (_wdhLock) {
        IOSimpleLockFree(_wdhLock);
        _wdhLock = NULL;
    }
    if (_hccaBuffer) {
        _hccaBuffer->release();
        _hccaBuffer = NULL;
    }
    if (_deviceBase) {
        _deviceBase->release();
        _deviceBase = NULL;
    }
    super::free();
}

IOReturn AppleUSBOHCI::UIMInitialize(IOService *)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMFinalize()
{
    _uimInitialized = false;
    return kIOReturnSuccess;
}

IOReturn AppleUSBOHCI::UIMOpenPipe(USBDeviceAddress, UInt8, Endpoint *)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMClosePipe(USBDeviceAddress, Endpoint *)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMAbortPipe(USBDeviceAddress, Endpoint *)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMClearPipeStall(USBDeviceAddress, Endpoint *)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMDeviceRequest(IOUSBDevRequest *, USBDeviceAddress)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMReadWrite(IOMemoryDescriptor *, USBDeviceAddress,
                                    Endpoint *, bool)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::AllocatePowerStateArray()
{
    return kIOReturnSuccess;
}

void AppleUSBOHCI::ResumeUSBBus(bool) {}
void AppleUSBOHCI::SuspendUSBBus(bool) {}
void AppleUSBOHCI::SetVendorInfo(void) {}
void AppleUSBOHCI::finishPending(void) {}
IOReturn AppleUSBOHCI::ControlInitialize(void) { return kIOReturnUnsupported; }
IOReturn AppleUSBOHCI::BulkInitialize(void) { return kIOReturnUnsupported; }
IOReturn AppleUSBOHCI::IsochronousInitialize(void) { return kIOReturnUnsupported; }
IOReturn AppleUSBOHCI::InterruptInitialize(void) { return kIOReturnUnsupported; }
IOReturn AppleUSBOHCI::InitializeOperationalRegisters(void) { return kIOReturnUnsupported; }
void AppleUSBOHCI::showRegisters(UInt32, const char *) {}

void AppleUSBOHCI::doCallback(AppleOHCIGeneralTransferDescriptorPtr, UInt32, UInt32) {}
UInt32 AppleUSBOHCI::findBufferRemaining(AppleOHCIGeneralTransferDescriptorPtr) { return 0; }
AppleOHCIIsochTransferDescriptorPtr AppleUSBOHCI::AllocateITD(void) { return NULL; }
AppleOHCIGeneralTransferDescriptorPtr AppleUSBOHCI::AllocateTD(void) { return NULL; }
AppleOHCIEndpointDescriptorPtr AppleUSBOHCI::AllocateED(void) { return NULL; }
IOReturn AppleUSBOHCI::TranslateStatusToUSBError(UInt32 status)
{
    return status ? kIOReturnIOError : kIOReturnSuccess;
}
void AppleUSBOHCI::ProcessCompletedITD(AppleOHCIIsochTransferDescriptorPtr, IOReturn) {}
IOReturn AppleUSBOHCI::DeallocateITD(AppleOHCIIsochTransferDescriptorPtr) { return kIOReturnSuccess; }
IOReturn AppleUSBOHCI::DeallocateTD(AppleOHCIGeneralTransferDescriptorPtr) { return kIOReturnSuccess; }
IOReturn AppleUSBOHCI::DeallocateED(AppleOHCIEndpointDescriptorPtr) { return kIOReturnSuccess; }
IOReturn AppleUSBOHCI::RemoveAllTDs(AppleOHCIEndpointDescriptorPtr) { return kIOReturnSuccess; }
IOReturn AppleUSBOHCI::RemoveTDs(AppleOHCIEndpointDescriptorPtr, bool) { return kIOReturnSuccess; }
IOReturn AppleUSBOHCI::DoDoneQueueProcessing(IOPhysicalAddress, UInt32, IOUSBCompletionAction)
{
    return kIOReturnSuccess;
}
void AppleUSBOHCI::UIMProcessDoneQueue(IOUSBCompletionAction) {}
void AppleUSBOHCI::UIMRootHubStatusChange(void) {}
void AppleUSBOHCI::UIMRootHubStatusChange(bool) {}
void AppleUSBOHCI::ReturnTransactions(AppleOHCIGeneralTransferDescriptorPtr, UInt32) {}
void AppleUSBOHCI::ReturnOneTransaction(AppleOHCIGeneralTransferDescriptorPtr,
                                        AppleOHCIEndpointDescriptorPtr,
                                        IOReturn) {}

IOReturn AppleUSBOHCI::UIMCreateControlEndpoint(UInt8, UInt8, UInt16, UInt8)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMCreateControlEndpoint(UInt8, UInt8, UInt16, UInt8,
                                                USBDeviceAddress, int)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMCreateControlTransfer(short, short, IOUSBCompletion,
                                                void *, bool, UInt32, short)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMCreateControlTransfer(short, short, IOUSBCommand *,
                                                void *, bool, UInt32, short)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMCreateControlTransfer(short, short, IOUSBCompletion,
                                                IOMemoryDescriptor *, bool,
                                                UInt32, short)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMCreateControlTransfer(short, short, IOUSBCommand *,
                                                IOMemoryDescriptor *, bool,
                                                UInt32, short)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMCreateBulkEndpoint(UInt8, UInt8, UInt8, UInt8, UInt8)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMCreateBulkEndpoint(UInt8, UInt8, UInt8, UInt8, UInt16,
                                             USBDeviceAddress, int)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMCreateBulkTransfer(short, short, IOUSBCompletion,
                                             IOMemoryDescriptor *, bool,
                                             UInt32, short)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMCreateBulkTransfer(IOUSBCommand *)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::CreateGeneralTransfer(AppleOHCIEndpointDescriptorPtr,
                                             IOUSBCommand *,
                                             IOMemoryDescriptor *,
                                             UInt32, UInt32, UInt32, UInt32)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMCreateInterruptEndpoint(short, short, UInt8, short,
                                                  UInt16, short)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMCreateInterruptEndpoint(short, short, UInt8, short,
                                                  UInt16, short,
                                                  USBDeviceAddress, int)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMCreateInterruptTransfer(short, short, IOUSBCompletion,
                                                  IOMemoryDescriptor *, bool,
                                                  UInt32, short)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMCreateInterruptTransfer(IOUSBCommand *)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMCreateIsochEndpoint(short, short, UInt32, UInt8)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMCreateIsochEndpoint(short, short, UInt32, UInt8,
                                              USBDeviceAddress, int)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMCreateIsochTransfer(short, short, IOUSBIsocCompletion,
                                              UInt8, UInt64,
                                              IOMemoryDescriptor *, UInt32,
                                              IOUSBIsocFrame *)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMCreateIsochTransfer(short, short, IOUSBIsocCompletion,
                                              UInt8, UInt64,
                                              IOMemoryDescriptor *, UInt32,
                                              IOUSBLowLatencyIsocFrame *,
                                              UInt32)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMCreateIsochTransfer(IOUSBIsocCommand *)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMAbortEndpoint(short, short, short)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMDeleteEndpoint(short, short, short)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBOHCI::UIMClearEndpointStall(short, short, short)
{
    return kIOReturnUnsupported;
}

UInt32 AppleUSBOHCI::GetBandwidthAvailable()
{
    return _isochBandwidthAvail;
}

UInt64 AppleUSBOHCI::GetFrameNumber()
{
    return _frameNumber;
}

UInt32 AppleUSBOHCI::GetFrameNumber32()
{
    return (UInt32)_frameNumber;
}

IOReturn AppleUSBOHCI::callPlatformFunction(const OSSymbol *, bool,
                                            void *, void *, void *, void *)
{
    return kIOReturnUnsupported;
}

void AppleUSBOHCI::PollInterrupts(IOUSBCompletionAction) {}
void AppleUSBOHCI::UIMCheckForTimeouts(void) {}

IOReturn AppleUSBOHCI::GetFrameNumberWithTime(UInt64 *frameNumber,
                                              AbsoluteTime *theTime)
{
    if (frameNumber)
        *frameNumber = GetFrameNumber();
    if (theTime)
        clock_get_uptime(theTime);
    return kIOReturnSuccess;
}

IOReturn AppleUSBOHCI::GatedGetFrameNumberWithTime(OSObject *owner,
                                                   void *arg0, void *arg1,
                                                   void *, void *)
{
    AppleUSBOHCI *me = OSDynamicCast(AppleUSBOHCI, owner);
    if (!me)
        return kIOReturnBadArgument;
    return me->GetFrameNumberWithTime((UInt64 *)arg0, (AbsoluteTime *)arg1);
}

IODMACommand *AppleUSBOHCI::GetNewDMACommand()
{
    return IODMACommand::withSpecification(kIODMACommandOutputHost32, 32, PAGE_SIZE,
        (IODMACommand::MappingOptions)(IODMACommand::kMapped | IODMACommand::kIterateOnly));
}

void AppleUSBOHCI::CheckSleepCapability(void) {}
IOReturn AppleUSBOHCI::ResetControllerState(void) { return kIOReturnUnsupported; }
IOReturn AppleUSBOHCI::RestartControllerFromReset(void) { return kIOReturnUnsupported; }
IOReturn AppleUSBOHCI::SaveControllerStateForSleep(void) { return kIOReturnSuccess; }
IOReturn AppleUSBOHCI::RestoreControllerStateFromSleep(void) { return kIOReturnUnsupported; }
IOReturn AppleUSBOHCI::DozeController(void) { return kIOReturnUnsupported; }
IOReturn AppleUSBOHCI::WakeControllerFromDoze(void) { return kIOReturnUnsupported; }
IOReturn AppleUSBOHCI::UIMEnableAddressEndpoints(USBDeviceAddress, bool)
{
    return kIOReturnUnsupported;
}
IOReturn AppleUSBOHCI::UIMEnableAllEndpoints(bool)
{
    return kIOReturnUnsupported;
}
IOReturn AppleUSBOHCI::EnableInterruptsFromController(bool) { return kIOReturnUnsupported; }
