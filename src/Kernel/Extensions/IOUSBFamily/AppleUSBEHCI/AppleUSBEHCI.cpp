#include <IOKit/IOLib.h>
#include <IOKit/IODMACommand.h>
#include "AppleUSBEHCI.h"

#define super IOUSBControllerV3
OSDefineMetaClassAndStructors(AppleUSBEHCI, IOUSBControllerV3)

#define kAssignedAddrKey "assigned-addresses"
#define EHCI_MMIO32_FALLBACK_BASE 0xF1000000ULL

static void
EHCI_Log(const char *fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    va_end(args);
    kprintf("[AppleUSBEHCI] %s\n", buf);
}

bool AppleUSBEHCI::init(OSDictionary *propTable)
{
    if (!super::init(propTable))
        return false;

    _device = NULL;
    _barDesc = NULL;
    _deviceBase = NULL;
    _capRegs = NULL;
    _opRegs = NULL;
    _frameNumber = 0;
    _bandwidthAvailable = 0;
    _capLength = 0;
    _rootHubPorts = 0;
    _uimInitialized = false;
    _asyncQHMem = NULL;
    _asyncQH = NULL;
    _asyncQHPhys = 0;
    _qTDMem = NULL;
    _qTDPool = NULL;
    _qTDPoolPhys = 0;
    return true;
}

bool AppleUSBEHCI::start(IOService *provider)
{
    if (!super::start(provider))
        return false;

    _device = OSDynamicCast(IOPCIDevice, provider);
    if (!_device)
        return false;

    _device->retain();
    _device->setMemoryEnable(true);
    _device->setBusMasterEnable(true);

    UInt16 vendor = _device->configRead16(kIOPCIConfigVendorID);
    UInt16 device = _device->configRead16(kIOPCIConfigDeviceID);
    UInt32 classCode = _device->configRead32(kIOPCIConfigRevisionID) >> 8;
    EHCI_Log("start provider=%p pci%x,%x pciclass,%06x", provider, vendor, device, classCode);

    if (!mapEHCIRegisters(_device)) {
        EHCI_Log("failed to map BAR0");
        return false;
    }

    _capLength = capRead8(kEHCICapLength);
    UInt16 version = capRead16(kEHCIHCIVersion);
    UInt32 hcs = capRead32(kEHCIHCSParams);
    UInt32 hcc = capRead32(kEHCIHCCParams);
    _rootHubPorts = EHCI_HCS_N_PORTS(hcs);

    EHCI_Log("caplen=%u version=%04x hcs=%08x hcc=%08x ports=%u eecp=%02x",
             _capLength, version, hcs, hcc, _rootHubPorts, EHCI_HCC_EECP(hcc));
    EHCI_Log("initial USBCMD=%08x USBSTS=%08x USBINTR=%08x CONFIGFLAG=%08x",
             opRead32(kEHCIUSBCmd), opRead32(kEHCIUSBSts),
             opRead32(kEHCIUSBIntr), opRead32(kEHCIConfigFlag));

    if (_capLength < 0x10 || version == 0xffff || version == 0) {
        EHCI_Log("invalid EHCI capability block");
        return false;
    }

    claimBIOSOwnership();
    if (!resetController()) {
        EHCI_Log("controller halt/reset failed");
        return false;
    }

    if (!setupAsyncSchedule()) {
        EHCI_Log("async schedule allocation failed");
        return false;
    }

    EHCI_Log("after reset USBCMD=%08x USBSTS=%08x USBINTR=%08x CONFIGFLAG=%08x",
             opRead32(kEHCIUSBCmd), opRead32(kEHCIUSBSts),
             opRead32(kEHCIUSBIntr), opRead32(kEHCIConfigFlag));

    EHCI_Log("register bring-up complete; async schedule allocated, EP0 not queued yet");
    return false;
}

void AppleUSBEHCI::stop(IOService *provider)
{
    UIMFinalize();
    super::stop(provider);
}

void AppleUSBEHCI::free()
{
    releaseHardwareResources();
    super::free();
}

UInt8 AppleUSBEHCI::capRead8(UInt32 offset)
{
    return *(volatile UInt8 *)(_capRegs + offset);
}

UInt16 AppleUSBEHCI::capRead16(UInt32 offset)
{
    return *(volatile UInt16 *)(_capRegs + offset);
}

UInt32 AppleUSBEHCI::capRead32(UInt32 offset)
{
    return *(volatile UInt32 *)(_capRegs + offset);
}

UInt32 AppleUSBEHCI::opRead32(UInt32 offset)
{
    return *(volatile UInt32 *)(_opRegs + offset);
}

void AppleUSBEHCI::opWrite32(UInt32 offset, UInt32 value)
{
    *(volatile UInt32 *)(_opRegs + offset) = value;
    __asm__ __volatile__("mfence" : : : "memory");
}

bool AppleUSBEHCI::mapEHCIRegisters(IOPCIDevice *provider)
{
    if (!provider)
        return false;

    IOMemoryMap *map = provider->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
    if (!map) {
        IODeviceMemory *range = provider->getDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
        if (range && range->getLength())
            map = range->map(kIOMapAnywhere);
    }

    if (!map) {
        OSData *assigned = OSDynamicCast(OSData, provider->copyProperty(kAssignedAddrKey));
        if (assigned) {
            const UInt8 *bytes = (const UInt8 *)assigned->getBytesNoCopy();
            UInt32 length = (UInt32)assigned->getLength();
            for (UInt32 off = 0; off + sizeof(IOPCIPhysicalAddress) <= length;
                 off += sizeof(IOPCIPhysicalAddress)) {
                const IOPCIPhysicalAddress *addr =
                    (const IOPCIPhysicalAddress *)(bytes + off);
                if (addr->physHi.s.registerNum != kIOPCIConfigBaseAddress0)
                    continue;
                if (addr->physHi.s.space == 1)
                    continue;

                UInt64 base = ((UInt64)addr->physMid << 32) | addr->physLo;
                UInt64 size = ((UInt64)addr->lengthHi << 32) | addr->lengthLo;
                EHCI_Log("assigned-addresses BAR0 space=%u base=0x%llx size=0x%llx",
                         addr->physHi.s.space, (unsigned long long)base,
                         (unsigned long long)size);
                if (!base)
                    continue;
                if (size < 0x1000)
                    size = 0x1000;

                _barDesc = IOMemoryDescriptor::withPhysicalAddress(
                    (IOPhysicalAddress)base, (IOByteCount)size,
                    kIODirectionNone | kIOMemoryMapperNone);
                if (_barDesc)
                    map = _barDesc->map(kIOMapAnywhere);
                if (map)
                    break;
                if (_barDesc) {
                    _barDesc->release();
                    _barDesc = NULL;
                }
            }
            assigned->release();
        }
    }

    if (!map) {
        UInt16 savedCmd = provider->configRead16(kIOPCIConfigCommand);
        UInt32 savedBar0 = provider->configRead32(kIOPCIConfigBaseAddress0);
        UInt32 savedBar1 = provider->configRead32(kIOPCIConfigBaseAddress1);
        bool is64 = (savedBar0 & 0x6U) == 0x4U;
        UInt64 base = 0;
        if (!(savedBar0 & 1U) && (savedBar0 & ~0x0fU) && savedBar0 != 0xffffffffU) {
            base = savedBar0 & ~0x0fULL;
            if (is64)
                base |= ((UInt64)savedBar1 << 32);
        }

        provider->configWrite16(kIOPCIConfigCommand, savedCmd & ~(UInt16)0x2);
        provider->configWrite32(kIOPCIConfigBaseAddress0, 0xffffffffU);
        UInt32 sizeMaskLo = provider->configRead32(kIOPCIConfigBaseAddress0);
        UInt32 sizeMaskHi = 0xffffffffU;
        if (is64) {
            provider->configWrite32(kIOPCIConfigBaseAddress1, 0xffffffffU);
            sizeMaskHi = provider->configRead32(kIOPCIConfigBaseAddress1);
        }
        provider->configWrite32(kIOPCIConfigBaseAddress0, savedBar0);
        if (is64)
            provider->configWrite32(kIOPCIConfigBaseAddress1, savedBar1);

        UInt64 mask = ((UInt64)sizeMaskHi << 32) | (sizeMaskLo & ~0x0fULL);
        UInt64 size = mask ? (~mask + 1) : 0;
        EHCI_Log("BAR0 sizing: saved=%08x:%08x is64=%u mask=%08x:%08x size=0x%llx",
                 savedBar1, savedBar0, is64, sizeMaskHi, sizeMaskLo,
                 (unsigned long long)size);

        if (!size || size > 0x1000000ULL)
            size = 0x1000;

        if (!base) {
            base = (EHCI_MMIO32_FALLBACK_BASE + (size - 1)) & ~(size - 1);
            provider->configWrite32(kIOPCIConfigBaseAddress0,
                                    (UInt32)(base & 0xfffffff0U) | (savedBar0 & 0x0fU));
            if (is64)
                provider->configWrite32(kIOPCIConfigBaseAddress1, (UInt32)(base >> 32));
            EHCI_Log("assigned fallback BAR0 base=0x%llx size=0x%llx",
                     (unsigned long long)base, (unsigned long long)size);
        }

        provider->configWrite16(kIOPCIConfigCommand, savedCmd | 0x2 | 0x4);
        _barDesc = IOMemoryDescriptor::withPhysicalAddress(
            (IOPhysicalAddress)base, (IOByteCount)size,
            kIODirectionNone | kIOMemoryMapperNone);
        if (_barDesc)
            map = _barDesc->map(kIOMapAnywhere);
    }

    if (!map) {
        UInt32 bar0 = provider->configRead32(kIOPCIConfigBaseAddress0);
        if (!(bar0 & 1U) && (bar0 & ~0x0fU) && bar0 != 0xffffffffU) {
            UInt64 base = bar0 & ~0x0fULL;
            if ((bar0 & 0x6U) == 0x4U)
                base |= ((UInt64)provider->configRead32(kIOPCIConfigBaseAddress1) << 32);
            _barDesc = IOMemoryDescriptor::withPhysicalAddress(
                (IOPhysicalAddress)base, 0x1000, kIODirectionNone | kIOMemoryMapperNone);
            if (_barDesc)
                map = _barDesc->map(kIOMapAnywhere);
            EHCI_Log("config BAR0 fallback base=0x%llx map=%p",
                     (unsigned long long)base, map);
        }
    }

    if (!map)
        return false;

    _deviceBase = map;
    _capRegs = (volatile UInt8 *)map->getVirtualAddress();
    _capLength = capRead8(kEHCICapLength);
    _opRegs = _capRegs + _capLength;
    EHCI_Log("mapped BAR0 virt=%p len=%llu caplen=%u",
             _capRegs, (unsigned long long)map->getLength(), _capLength);
    return true;
}

void AppleUSBEHCI::claimBIOSOwnership(void)
{
    UInt32 hcc = capRead32(kEHCIHCCParams);
    UInt8 eecp = EHCI_HCC_EECP(hcc);
    if (!eecp || eecp < 0x40)
        return;

    for (UInt8 off = eecp; off; ) {
        UInt32 cap = _device->configRead32(off);
        UInt8 capId = cap & 0xffU;
        UInt8 next = (cap >> 8) & 0xffU;
        if (capId == 1) {
            EHCI_Log("legacy support cap @%02x value=%08x", off, cap);
            if (cap & (1U << 16)) {
                _device->configWrite32(off, cap | (1U << 24));
                for (int i = 0; i < 100; i++) {
                    cap = _device->configRead32(off);
                    if (!(cap & (1U << 16)) && (cap & (1U << 24)))
                        break;
                    IOSleep(10);
                }
                EHCI_Log("legacy support after handoff value=%08x", _device->configRead32(off));
            }
            if (next && next >= 0x40)
                off = next;
            else
                break;
        } else {
            if (next && next >= 0x40)
                off = next;
            else
                break;
        }
    }
}

bool AppleUSBEHCI::haltController(void)
{
    UInt32 cmd = opRead32(kEHCIUSBCmd);
    cmd &= ~(kEHCIUSBCmdRunStop | kEHCIUSBCmdAsyncEnable | kEHCIUSBCmdPeriodicEnable);
    opWrite32(kEHCIUSBCmd, cmd);
    opWrite32(kEHCIUSBIntr, 0);

    for (int i = 0; i < 100; i++) {
        if (opRead32(kEHCIUSBSts) & kEHCIUSBStsHalted)
            return true;
        IOSleep(10);
    }
    return false;
}

bool AppleUSBEHCI::resetController(void)
{
    if (!haltController())
        return false;

    UInt32 cmd = opRead32(kEHCIUSBCmd);
    opWrite32(kEHCIUSBCmd, cmd | kEHCIUSBCmdHCReset);
    for (int i = 0; i < 100; i++) {
        if (!(opRead32(kEHCIUSBCmd) & kEHCIUSBCmdHCReset))
            return true;
        IOSleep(10);
    }
    return false;
}

bool AppleUSBEHCI::setupAsyncSchedule(void)
{
    _asyncQHMem = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIOMemoryUnshared | kIODirectionInOut, kEHCIPageSize,
        0x00000000fffff000ULL);
    if (!_asyncQHMem || _asyncQHMem->prepare() != kIOReturnSuccess)
        return false;

    _asyncQH = (EHCIAsyncQueueHeadPtr)_asyncQHMem->getBytesNoCopy();
    _asyncQHPhys = (USBPhysicalAddress32)_asyncQHMem->getPhysicalAddress();
    bzero(_asyncQH, kEHCIPageSize);

    _qTDMem = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIOMemoryUnshared | kIODirectionInOut, kEHCIPageSize,
        0x00000000fffff000ULL);
    if (!_qTDMem || _qTDMem->prepare() != kIOReturnSuccess)
        return false;

    _qTDPool = (EHCIGeneralTransferDescriptorSharedPtr)_qTDMem->getBytesNoCopy();
    _qTDPoolPhys = (USBPhysicalAddress32)_qTDMem->getPhysicalAddress();
    bzero(_qTDPool, kEHCIPageSize);

    _asyncQH->horizLinkPtr = HostToUSBLong((_asyncQHPhys & kEHCILinkPointerMask) | kEHCILinkTypeQH);
    _asyncQH->endpointCaps = HostToUSBLong(kEHCIQHHead | kEHCIQHDTC |
                                           kEHCIQHEndpointSpeedHigh |
                                           (64U << kEHCIQHMaxPacketShift));
    _asyncQH->endpointSplitCaps = 0;
    _asyncQH->currentqTDPtr = 0;
    _asyncQH->nextqTDPtr = HostToUSBLong(kEHCIqTDTerminate);
    _asyncQH->altqTDPtr = HostToUSBLong(kEHCIqTDTerminate);
    _asyncQH->qTDFlags = HostToUSBLong(kEHCIqTDStatusHalted);

    opWrite32(kEHCIAsyncListAddr, _asyncQHPhys & kEHCILinkPointerMask);
    EHCI_Log("async schedule QH virt=%p phys=%08x qTD virt=%p phys=%08x ASYNCLISTADDR=%08x",
             _asyncQH, _asyncQHPhys, _qTDPool, _qTDPoolPhys, opRead32(kEHCIAsyncListAddr));
    return true;
}

bool AppleUSBEHCI::enableAsyncSchedule(bool enable)
{
    UInt32 cmd = opRead32(kEHCIUSBCmd);
    if (enable)
        cmd |= kEHCIUSBCmdAsyncEnable;
    else
        cmd &= ~kEHCIUSBCmdAsyncEnable;
    opWrite32(kEHCIUSBCmd, cmd);

    for (int i = 0; i < 100; i++) {
        bool status = (opRead32(kEHCIUSBSts) & kEHCIUSBStsAsyncStatus) != 0;
        if (status == enable)
            return true;
        IOSleep(10);
    }
    return false;
}

bool AppleUSBEHCI::runController(bool run)
{
    UInt32 cmd = opRead32(kEHCIUSBCmd);
    cmd &= ~(UInt32)0x0c;
    cmd |= kEHCIUSBCmdFrameListSize1024;
    if (run)
        cmd |= kEHCIUSBCmdRunStop;
    else
        cmd &= ~kEHCIUSBCmdRunStop;
    opWrite32(kEHCIUSBCmd, cmd);

    for (int i = 0; i < 100; i++) {
        bool halted = (opRead32(kEHCIUSBSts) & kEHCIUSBStsHalted) != 0;
        if (halted != run)
            return true;
        IOSleep(10);
    }
    return false;
}

EHCIGeneralTransferDescriptorSharedPtr AppleUSBEHCI::qTDAt(UInt32 index)
{
    UInt32 count = kEHCIPageSize / sizeof(EHCIGeneralTransferDescriptorShared);
    if (!_qTDPool || index >= count)
        return NULL;
    return &_qTDPool[index];
}

USBPhysicalAddress32 AppleUSBEHCI::qTDPhys(UInt32 index)
{
    return _qTDPoolPhys + index * sizeof(EHCIGeneralTransferDescriptorShared);
}

void AppleUSBEHCI::setQTDBuffer(EHCIGeneralTransferDescriptorSharedPtr qtd,
                                USBPhysicalAddress32 phys, UInt32 length)
{
    UInt32 addr = phys;
    UInt32 end = addr + length;
    UInt32 page = (addr & 0xfffff000U) + 0x1000U;

    for (int i = 0; i < 5; i++) {
        qtd->bufferPtr[i] = 0;
        qtd->extBufferPtr[i] = 0;
    }

    if (!length)
        return;

    qtd->bufferPtr[0] = HostToUSBLong(addr);
    for (int i = 1; i < 5 && page < end; i++, page += 0x1000U)
        qtd->bufferPtr[i] = HostToUSBLong(page);
}

void AppleUSBEHCI::fillQTD(EHCIGeneralTransferDescriptorSharedPtr qtd,
                           USBPhysicalAddress32 next, UInt32 pid, UInt32 dataToggle,
                           USBPhysicalAddress32 buffer, UInt32 length,
                           bool interruptOnComplete)
{
    bzero(qtd, sizeof(*qtd));
    qtd->nextTD = HostToUSBLong(next ? next : kEHCIqTDTerminate);
    qtd->altTD = HostToUSBLong(kEHCIqTDTerminate);
    setQTDBuffer(qtd, buffer, length);

    UInt32 token = kEHCIqTDStatusActive | pid | (3U << kEHCIqTDCerrShift) |
                   ((length & 0x7fffU) << kEHCIqTDBytesShift);
    if (dataToggle)
        token |= kEHCIqTDDataToggle;
    if (interruptOnComplete)
        token |= kEHCIqTDIOC;
    qtd->flags = HostToUSBLong(token);
}

IOReturn AppleUSBEHCI::waitForQTD(EHCIGeneralTransferDescriptorSharedPtr qtd,
                                  UInt32 timeoutMs)
{
    for (UInt32 i = 0; i < timeoutMs; i++) {
        UInt32 token = USBToHostLong(qtd->flags);
        if (!(token & kEHCIqTDStatusActive)) {
            if (token & kEHCIqTDStatusHalted)
                return kIOReturnIOError;
            return kIOReturnSuccess;
        }
        IOSleep(1);
    }
    return kIOReturnTimeout;
}

IOReturn AppleUSBEHCI::controlTransfer(USBDeviceAddress address,
                                       IOUSBDevRequest *request)
{
    if (!_asyncQH || !_qTDPool || !request)
        return kIOReturnNotReady;
    if (request->wLength && !request->pData)
        return kIOReturnBadArgument;

    IOBufferMemoryDescriptor *setupMem = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIOMemoryPhysicallyContiguous | kIODirectionInOut, 8,
        0x00000000ffffffffULL);
    if (!setupMem || setupMem->prepare() != kIOReturnSuccess) {
        if (setupMem)
            setupMem->release();
        return kIOReturnNoMemory;
    }

    UInt8 *setup = (UInt8 *)setupMem->getBytesNoCopy();
    setup[0] = request->bmRequestType;
    setup[1] = request->bRequest;
    setup[2] = request->wValue & 0xff;
    setup[3] = request->wValue >> 8;
    setup[4] = request->wIndex & 0xff;
    setup[5] = request->wIndex >> 8;
    setup[6] = request->wLength & 0xff;
    setup[7] = request->wLength >> 8;

    IOBufferMemoryDescriptor *dataMem = NULL;
    UInt8 *data = NULL;
    UInt32 dataLength = request->wLength;
    bool dataIn = (request->bmRequestType & 0x80) != 0;

    if (dataLength) {
        dataMem = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
            kernel_task, kIOMemoryPhysicallyContiguous | kIODirectionInOut,
            dataLength, 0x00000000ffffffffULL);
        if (!dataMem || dataMem->prepare() != kIOReturnSuccess) {
            if (dataMem)
                dataMem->release();
            setupMem->complete();
            setupMem->release();
            return kIOReturnNoMemory;
        }
        data = (UInt8 *)dataMem->getBytesNoCopy();
        if (dataIn)
            bzero(data, dataLength);
        else
            bcopy(request->pData, data, dataLength);
    }

    EHCIGeneralTransferDescriptorSharedPtr setupTD = qTDAt(0);
    EHCIGeneralTransferDescriptorSharedPtr dataTD = qTDAt(1);
    EHCIGeneralTransferDescriptorSharedPtr statusTD = qTDAt(2);
    USBPhysicalAddress32 setupTDPhys = qTDPhys(0);
    USBPhysicalAddress32 dataTDPhys = qTDPhys(1);
    USBPhysicalAddress32 statusTDPhys = qTDPhys(2);

    fillQTD(setupTD, dataLength ? dataTDPhys : statusTDPhys, kEHCIqTDPIDSetup, 0,
            (USBPhysicalAddress32)setupMem->getPhysicalAddress(), 8, false);
    if (dataLength) {
        fillQTD(dataTD, statusTDPhys, dataIn ? kEHCIqTDPIDIn : kEHCIqTDPIDOut, 1,
                (USBPhysicalAddress32)dataMem->getPhysicalAddress(), dataLength, false);
    } else {
        bzero(dataTD, sizeof(*dataTD));
    }
    fillQTD(statusTD, 0, dataIn ? kEHCIqTDPIDOut : kEHCIqTDPIDIn, 1, 0, 0, true);

    _asyncQH->endpointCaps = HostToUSBLong(kEHCIQHHead | kEHCIQHDTC |
                                           kEHCIQHEndpointSpeedHigh |
                                           ((UInt32)address & 0x7fU) |
                                           (64U << kEHCIQHMaxPacketShift));
    _asyncQH->endpointSplitCaps = 0;
    _asyncQH->currentqTDPtr = 0;
    _asyncQH->nextqTDPtr = HostToUSBLong(setupTDPhys);
    _asyncQH->altqTDPtr = HostToUSBLong(kEHCIqTDTerminate);
    _asyncQH->qTDFlags = 0;
    for (int i = 0; i < 5; i++) {
        _asyncQH->bufferPtr[i] = 0;
        _asyncQH->extBufferPtr[i] = 0;
    }

    if (!runController(true)) {
        EHCI_Log("control transfer: controller failed to run");
        request->wLenDone = 0;
        if (dataMem) {
            dataMem->complete();
            dataMem->release();
        }
        setupMem->complete();
        setupMem->release();
        return kIOReturnTimeout;
    }
    opWrite32(kEHCIConfigFlag, 1);

    IOReturn ret = enableAsyncSchedule(true) ? waitForQTD(statusTD, 1000)
                                             : kIOReturnTimeout;
    enableAsyncSchedule(false);

    UInt32 setupToken = USBToHostLong(setupTD->flags);
    UInt32 dataToken = dataLength ? USBToHostLong(dataTD->flags) : 0;
    UInt32 statusToken = USBToHostLong(statusTD->flags);
    if (ret == kIOReturnSuccess &&
        ((setupToken | dataToken | statusToken) & kEHCIqTDStatusHalted))
        ret = kIOReturnIOError;

    request->wLenDone = 0;
    if (ret == kIOReturnSuccess && dataLength) {
        UInt32 remaining = (dataToken & kEHCIqTDBytesMask) >> kEHCIqTDBytesShift;
        request->wLenDone = dataLength - remaining;
        if (dataIn && request->wLenDone)
            bcopy(data, request->pData, request->wLenDone);
    } else if (ret == kIOReturnSuccess) {
        request->wLenDone = 0;
    }

    EHCI_Log("control addr=%u req=%u type=%02x len=%u ret=%08x done=%u td=%08x/%08x/%08x",
             address, request->bRequest, request->bmRequestType, request->wLength,
             ret, request->wLenDone, setupToken, dataToken, statusToken);

    _asyncQH->nextqTDPtr = HostToUSBLong(kEHCIqTDTerminate);
    _asyncQH->qTDFlags = HostToUSBLong(kEHCIqTDStatusHalted);

    if (dataMem) {
        dataMem->complete();
        dataMem->release();
    }
    setupMem->complete();
    setupMem->release();
    return ret;
}

void AppleUSBEHCI::releaseAsyncSchedule(void)
{
    if (_qTDMem) {
        _qTDMem->complete();
        _qTDMem->release();
        _qTDMem = NULL;
    }
    if (_asyncQHMem) {
        _asyncQHMem->complete();
        _asyncQHMem->release();
        _asyncQHMem = NULL;
    }
    _asyncQH = NULL;
    _qTDPool = NULL;
    _asyncQHPhys = 0;
    _qTDPoolPhys = 0;
}

void AppleUSBEHCI::releaseHardwareResources(void)
{
    if (_opRegs)
        haltController();
    releaseAsyncSchedule();
    if (_deviceBase) {
        _deviceBase->release();
        _deviceBase = NULL;
    }
    if (_barDesc) {
        _barDesc->release();
        _barDesc = NULL;
    }
    if (_device) {
        _device->release();
        _device = NULL;
    }
    _capRegs = NULL;
    _opRegs = NULL;
}

IOReturn AppleUSBEHCI::UIMOpenPipe(USBDeviceAddress, UInt8, Endpoint *)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBEHCI::UIMClosePipe(USBDeviceAddress, Endpoint *)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBEHCI::UIMAbortPipe(USBDeviceAddress, Endpoint *)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBEHCI::UIMClearPipeStall(USBDeviceAddress, Endpoint *)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBEHCI::UIMDeviceRequest(IOUSBDevRequest *request, USBDeviceAddress address)
{
    return controlTransfer(address, request);
}

IOReturn AppleUSBEHCI::UIMReadWrite(IOMemoryDescriptor *, USBDeviceAddress,
                                    Endpoint *, bool)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBEHCI::UIMInitialize(IOService *)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBEHCI::UIMFinalize()
{
    _uimInitialized = false;
    return kIOReturnSuccess;
}

IOReturn AppleUSBEHCI::UIMCreateControlEndpoint(UInt8, UInt8, UInt16, UInt8,
                                                USBDeviceAddress, int)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBEHCI::UIMCreateBulkEndpoint(UInt8, UInt8, UInt8, UInt8, UInt16,
                                             USBDeviceAddress, int)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBEHCI::UIMCreateInterruptEndpoint(short, short, UInt8, short,
                                                  UInt16, short,
                                                  USBDeviceAddress, int)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBEHCI::UIMCreateIsochEndpoint(short, short, UInt32, UInt8,
                                              USBDeviceAddress, int)
{
    return kIOReturnUnsupported;
}

UInt32 AppleUSBEHCI::GetBandwidthAvailable()
{
    return _bandwidthAvailable;
}

UInt64 AppleUSBEHCI::GetFrameNumber()
{
    return _frameNumber;
}

UInt32 AppleUSBEHCI::GetFrameNumber32()
{
    return (UInt32)_frameNumber;
}

IOReturn AppleUSBEHCI::GetFrameNumberWithTime(UInt64 *frameNumber,
                                              AbsoluteTime *theTime)
{
    if (frameNumber)
        *frameNumber = GetFrameNumber();
    if (theTime)
        clock_get_uptime(theTime);
    return kIOReturnSuccess;
}

IOReturn AppleUSBEHCI::GatedGetFrameNumberWithTime(OSObject *owner,
                                                   void *arg0, void *arg1,
                                                   void *, void *)
{
    AppleUSBEHCI *me = OSDynamicCast(AppleUSBEHCI, owner);
    if (!me)
        return kIOReturnBadArgument;
    return me->GetFrameNumberWithTime((UInt64 *)arg0, (AbsoluteTime *)arg1);
}

IODMACommand *AppleUSBEHCI::GetNewDMACommand()
{
    return IODMACommand::withSpecification(kIODMACommandOutputHost32, 32, PAGE_SIZE,
        (IODMACommand::MappingOptions)(IODMACommand::kMapped | IODMACommand::kIterateOnly));
}

IOReturn AppleUSBEHCI::ResetControllerState()
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBEHCI::RestartControllerFromReset()
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBEHCI::SaveControllerStateForSleep()
{
    return kIOReturnSuccess;
}

IOReturn AppleUSBEHCI::RestoreControllerStateFromSleep()
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBEHCI::DozeController()
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBEHCI::WakeControllerFromDoze()
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBEHCI::UIMEnableAddressEndpoints(USBDeviceAddress, bool)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBEHCI::UIMEnableAllEndpoints(bool)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBEHCI::EnableInterruptsFromController(bool)
{
    return kIOReturnUnsupported;
}

IOReturn AppleUSBEHCI::DeallocateITD(EHCIIsochTransferDescriptorPtr)
{
    return kIOReturnSuccess;
}

IOReturn AppleUSBEHCI::DeallocateSITD(EHCISplitIsochTransferDescriptorPtr)
{
    return kIOReturnSuccess;
}
