/*
 * RavynXHCIPort: minimal xHCI host controller driver + USB Mass Storage
 * (bulk-only transport) enumeration.
 *
 * Copyright (C) 2026 ravynOS Project. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <IOKit/IOLib.h>
#include <IOKit/storage/IOMedia.h>
#include "RavynXHCIPort.h"
#include "RavynXHCIMassStorageDisk.h"

#define super IOService
OSDefineMetaClassAndStructors(RavynXHCIPort, IOService);

#define kAssignedAddrKey "assigned-addresses"
#define kRingTRBs   256   /* TRBs per ring segment, last one reserved for LINK */

void XHCI_Log(const char *fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf) - 1, fmt, args);
    kprintf("[RavynXHCIPort] %s\n", buf);
}

static IOMemoryMap *
mapUsableBAR(IOPCIDevice *provider, UInt8 reg)
{
    if (!provider) return NULL;
    IODeviceMemory *range = provider->getDeviceMemoryWithRegister(reg);
    if (!range) {
        XHCI_Log("BAR%u has no IODeviceMemory range", (reg - kIOPCIConfigBaseAddress0) / 4);
        return NULL;
    }
    IOByteCount length = range->getLength();
    if (!length) {
        XHCI_Log("BAR%u IODeviceMemory has zero length; skipping provider map",
                (reg - kIOPCIConfigBaseAddress0) / 4);
        return NULL;
    }
    return range->map(kIOMapAnywhere);
}

static bool
mapBARFromAssignedAddresses(IOPCIDevice         * provider,
                            IOMemoryDescriptor ** outDesc,
                            IOMemoryMap        ** outMap)
{
    if (!provider || !outDesc || !outMap) return false;

    OSData *assigned = OSDynamicCast(OSData, provider->copyProperty(kAssignedAddrKey));
    if (!assigned) {
        XHCI_Log("no assigned-addresses property");
        return false;
    }

    const uint32_t kEntrySize = sizeof(IOPCIPhysicalAddress);
    const uint8_t * bytes = (const uint8_t *)assigned->getBytesNoCopy();
    const uint32_t len = (uint32_t)assigned->getLength();

    for (uint32_t off = 0; off + kEntrySize <= len; off += kEntrySize) {
        const IOPCIPhysicalAddress *a = (const IOPCIPhysicalAddress *)(bytes + off);
        const uint8_t reg = a->physHi.s.registerNum;
        uint64_t base0 = ((uint64_t)a->physMid << 32) | a->physLo;
        uint64_t size0 = ((uint64_t)a->lengthHi << 32) | a->lengthLo;
        XHCI_Log("assigned-addresses reg=%u space=%u base=%p size=%llu",
                (reg - kIOPCIConfigBaseAddress0) / 4, a->physHi.s.space,
                (void *)(uintptr_t)base0, (uint64_t)size0);
        if (reg != kIOPCIConfigBaseAddress0)
            continue;
        if (a->physHi.s.space == 1) continue; /* I/O space */

        uint64_t base = base0;
        uint64_t size = size0;
        if (!base || !size) continue;
        if (size < 0x2000) size = 0x2000;

        IOMemoryDescriptor *desc = IOMemoryDescriptor::withPhysicalAddress(
            (IOPhysicalAddress)base, (IOByteCount)size, kIODirectionNone | kIOMemoryMapperNone);
        if (!desc) continue;
        IOMemoryMap *map = desc->map(kIOMapAnywhere);
        if (!map) { desc->release(); continue; }

        *outDesc = desc;
        *outMap = map;
        assigned->release();
        return true;
    }
    assigned->release();
    return false;
}

/* Some firmware (observed with QEMU's qemu-xhci) never sizes/assigns BAR0 at
 * all - it reads back as just the type bits (e.g. 0x00000004: 64-bit MMIO,
 * memory space, zero address). Real PCI BIOS/UEFI normally does this during
 * POST; when it hasn't, do the standard BAR-sizing dance ourselves: disable
 * decode, write all-1s, read back the size mask, then program a real base.
 * MMIO32_FALLBACK_BASE sits just past the MCFG ECAM window (observed at
 * 0xE0000000, spanning 256 buses * 1MB = 0x10000000, ending at 0xF0000000)
 * - a conventional, normally-unused gap on q35-style chipsets. */
#define MMIO32_FALLBACK_BASE 0xF0000000ULL

/* Always run the standard BAR-sizing dance (disable decode, write all-1s,
 * read back the size mask, restore) to learn the BAR's real size - needed
 * even when firmware DID assign a real base: real hardware observed with
 * caplen=128/dboff=0x3000/rtsoff=0x2000 needs far more than the 0x2000-byte
 * window we used to hardcode for "already assigned" BARs, and reading/
 * writing the doorbell array past the end of an undersized mapping faults
 * (this is what caused the real-hardware panic in ringDoorbell()). If
 * *outBase is already nonzero on entry, only the size is (re)computed and
 * the existing base is preserved; if it's zero, a new base is chosen and
 * programmed (the QEMU qemu-xhci case, where firmware left BAR0 completely
 * unassigned). */
static bool
sizeAndAssignBAR0(IOPCIDevice *provider, uint64_t *outBase, uint64_t *outSize)
{
    const bool haveBase = (*outBase != 0);
    const uint16_t cmd = provider->configRead16(kIOPCIConfigCommand);
    provider->configWrite16(kIOPCIConfigCommand, cmd & ~(uint16_t)0x2 /* memory space */);

    const uint32_t origBar0 = provider->configRead32(kIOPCIConfigBaseAddress0);
    const uint32_t origBar1 = provider->configRead32(kIOPCIConfigBaseAddress1);
    const bool is64 = (origBar0 & 0x6) == 0x4;

    provider->configWrite32(kIOPCIConfigBaseAddress0, 0xFFFFFFFFU);
    uint32_t sizeMaskLo = provider->configRead32(kIOPCIConfigBaseAddress0);
    uint32_t sizeMaskHi = 0xFFFFFFFFU;
    if (is64) {
        provider->configWrite32(kIOPCIConfigBaseAddress1, 0xFFFFFFFFU);
        sizeMaskHi = provider->configRead32(kIOPCIConfigBaseAddress1);
    }

    uint64_t mask = ((uint64_t)sizeMaskHi << 32) | (sizeMaskLo & ~0xFU);
    uint64_t size = mask ? (~mask + 1) : 0;
    XHCI_Log("BAR0 sizing: is64=%d sizeMaskLo=%08x sizeMaskHi=%08x -> size=0x%llx",
            is64, sizeMaskLo, sizeMaskHi, (unsigned long long)size);

    /* Always restore the original BARs before reprogramming anything
     * origBar0/origBar1 hold the real base when haveBase is true. */
    provider->configWrite32(kIOPCIConfigBaseAddress0, origBar0);
    if (is64) provider->configWrite32(kIOPCIConfigBaseAddress1, origBar1);

    if (!size || size > 0x10000000ULL /* 256MB sanity cap */) {
        provider->configWrite16(kIOPCIConfigCommand, cmd);
        return false;
    }

    uint64_t base = *outBase;
    if (!haveBase) {
        base = (MMIO32_FALLBACK_BASE + (size - 1)) & ~(size - 1);
        provider->configWrite32(kIOPCIConfigBaseAddress0, (uint32_t)(base & 0xFFFFFFF0U) | (origBar0 & 0xFU));
        if (is64) provider->configWrite32(kIOPCIConfigBaseAddress1, (uint32_t)(base >> 32));
    }

    provider->configWrite16(kIOPCIConfigCommand, cmd | 0x2 /* memory space */ | 0x4 /* bus master */);

    XHCI_Log("BAR0 %s base=0x%llx size=0x%llx", haveBase ? "sized" : "assigned",
            (unsigned long long)base, (unsigned long long)size);
    *outBase = base;
    *outSize = size;
    return true;
}

static bool
mapBARFromConfig(IOPCIDevice         * provider,
                 IOMemoryDescriptor ** outDesc,
                 IOMemoryMap        ** outMap)
{
    if (!provider || !outDesc || !outMap) return false;
    const uint16_t cmd  = provider->configRead16(kIOPCIConfigCommand);
    const uint32_t bar0 = provider->configRead32(kIOPCIConfigBaseAddress0);
    const uint32_t bar1 = provider->configRead32(kIOPCIConfigBaseAddress1);
    XHCI_Log("config fallback CMD=%04x BAR0=%08x BAR1=%08x", cmd, bar0, bar1);
    uint64_t phys = 0;
    uint64_t size = 0x2000;
    if (!(bar0 & 0x1) && bar0 != 0xffffffffU && (bar0 & ~0xfU)) {
        phys = (uint64_t)(bar0 & ~0xfU);
        if ((bar0 & 0x6) == 0x4) phys |= ((uint64_t)bar1 << 32);
    }
    {
        XHCI_Log("config fallback: %s BAR0, sizing it", phys ? "have" : "no");
        uint64_t base = phys, realSize = 0;
        if (!sizeAndAssignBAR0(provider, &base, &realSize)) {
            XHCI_Log("config fallback: BAR0 sizing failed");
            if (!phys) return false;
            /* Fall back to the hardcoded minimum for an already-assigned BAR
             * whose size we simply couldn't determine. */
        } else {
            phys = base;
            size = realSize;
        }
    }

    IOMemoryDescriptor *desc = IOMemoryDescriptor::withPhysicalAddress(
        (IOPhysicalAddress)phys, (IOByteCount)size, kIODirectionNone | kIOMemoryMapperNone);
    if (!desc) return false;
    IOMemoryMap *map = desc->map(kIOMapAnywhere);
    if (!map) { desc->release(); return false; }
    *outDesc = desc;
    *outMap = map;
    return true;
}

IOService *
RavynXHCIPort::probe(IOService *provider, SInt32 *score)
{
    IOPCIDevice *pci = OSDynamicCast(IOPCIDevice, provider);
    if (!pci) return NULL;
    if (score) *score += 1000;
    return super::probe(provider, score);
}

bool RavynXHCIPort::start(IOService *provider)
{
    fProvider = OSDynamicCast(IOPCIDevice, provider);
    if (!fProvider || !super::start(provider)) return false;

    fProvider->retain();
    fProvider->setMemoryEnable(true);
    fProvider->setBusMasterEnable(true);

    fCmdLock = IOLockAlloc();
    if (!fCmdLock) {
        XHCI_Log("Failed to alloc command lock");
        return false;
    }

    uint16_t vendor = fProvider->configRead16(kIOPCIConfigVendorID);
    uint16_t device = fProvider->configRead16(kIOPCIConfigDeviceID);
    uint32_t classCode = fProvider->configRead32(kIOPCIConfigRevisionID) >> 8;
    XHCI_Log("start provider=%p pci%x,%x pciclass,%06x", provider, vendor, device, classCode);

    fBARMap = mapUsableBAR(fProvider, kIOPCIConfigBaseAddress0);
    if (!fBARMap) mapBARFromAssignedAddresses(fProvider, &fBARDesc, &fBARMap);
    if (!fBARMap) mapBARFromConfig(fProvider, &fBARDesc, &fBARMap);
    if (!fBARMap) {
        XHCI_Log("Failed to map BAR0!");
        return false;
    }

    fCapRegs = (volatile UInt8 *)fBARMap->getVirtualAddress();
    UInt8 capLength = *(volatile UInt8 *)(fCapRegs + XHCI_CAPLENGTH);
    fOpRegs = fCapRegs + capLength;

    UInt32 hcsp1 = capRead32(XHCI_HCSPARAMS1);
    UInt32 hccp1 = capRead32(XHCI_HCCPARAMS1);
    fMaxSlots = XHCI_HCSP1_MAXSLOTS(hcsp1);
    fMaxPorts = XHCI_HCSP1_MAXPORTS(hcsp1);
    fContextSize = XHCI_HCCP1_CSZ(hccp1) ? 64 : 32;

    UInt32 dboff = capRead32(XHCI_DBOFF) & ~0x3U;
    UInt32 rtsoff = capRead32(XHCI_RTSOFF) & ~0x1FU;
    fDBRegs = fCapRegs + dboff;
    fRTRegs = fCapRegs + rtsoff;

    XHCI_Log("caplen=%u maxslots=%u maxports=%u ctxsize=%u dboff=%x rtsoff=%x",
            capLength, fMaxSlots, fMaxPorts, fContextSize, dboff, rtsoff);

    if (fContextSize != 32) {
        /* 64-byte contexts (CSZ=1) need every context-array struct doubled up;
         * not implemented - bail cleanly rather than corrupt DMA memory. */
        XHCI_Log("64-byte contexts not supported, refusing to attach");
        return false;
    }

    if (!resetController()) {
        XHCI_Log("controller reset failed");
        return false;
    }
    XHCI_Log("checkpoint after resetController: USBCMD=%08x USBSTS=%08x",
            opRead32(XHCI_USBCMD), opRead32(XHCI_USBSTS));

    if (!setupDCBAA() || !setupCommandRing() || !setupEventRing()) {
        XHCI_Log("ring/DCBAA setup failed");
        return false;
    }
    XHCI_Log("checkpoint after ring/DCBAA setup: USBCMD=%08x USBSTS=%08x",
            opRead32(XHCI_USBCMD), opRead32(XHCI_USBSTS));

    /* CONFIG.MaxSlotsEn */
    opWrite32(XHCI_CONFIG, fMaxSlots);
    XHCI_Log("checkpoint after CONFIG write: USBCMD=%08x USBSTS=%08x",
            opRead32(XHCI_USBCMD), opRead32(XHCI_USBSTS));

    /* Run */
    opWrite32(XHCI_USBCMD, opRead32(XHCI_USBCMD) | XHCI_USBCMD_RS);
    XHCI_Log("checkpoint after RS write: USBCMD=%08x USBSTS=%08x",
            opRead32(XHCI_USBCMD), opRead32(XHCI_USBSTS));

    bzero(fMSC, sizeof(fMSC));
    bzero(fDiskNubs, sizeof(fDiskNubs));
    bzero(fSlots, sizeof(fSlots));

    scanPorts();

    registerService();
    return true;
}

void RavynXHCIPort::stop(IOService *provider)
{
    super::stop(provider);
}

void RavynXHCIPort::free()
{
    for (int i = 0; i < 16; i++) {
        if (fDiskNubs[i]) { fDiskNubs[i]->release(); fDiskNubs[i] = NULL; }
    }
    if (fCmdLock) { IOLockFree(fCmdLock); fCmdLock = NULL; }
    if (fBARMap) { fBARMap->release(); fBARMap = NULL; }
    if (fBARDesc) { fBARDesc->release(); fBARDesc = NULL; }
    if (fProvider) { fProvider->release(); fProvider = NULL; }
    super::free();
}

bool RavynXHCIPort::resetController()
{
    /* Stop if running. */
    if (!(opRead32(XHCI_USBSTS) & XHCI_USBSTS_HCH)) {
        opWrite32(XHCI_USBCMD, opRead32(XHCI_USBCMD) & ~XHCI_USBCMD_RS);
        for (int i = 0; i < 2000 && !(opRead32(XHCI_USBSTS) & XHCI_USBSTS_HCH); i++)
            IOSleep(1);
    }

    opWrite32(XHCI_USBCMD, opRead32(XHCI_USBCMD) | XHCI_USBCMD_HCRST);
    for (int i = 0; i < 2000; i++) {
        if (!(opRead32(XHCI_USBCMD) & XHCI_USBCMD_HCRST) &&
            !(opRead32(XHCI_USBSTS) & XHCI_USBSTS_CNR))
            return true;
        IOSleep(1);
    }
    XHCI_Log("reset timed out USBCMD=%08x USBSTS=%08x", opRead32(XHCI_USBCMD), opRead32(XHCI_USBSTS));
    return false;
}

bool RavynXHCIPort::setupDCBAA()
{
    UInt64 mask = 0xFFFFFFFFFFFFFFFFULL;
    fDCBAAMem = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIOMemoryPhysicallyContiguous | kIODirectionInOut,
        (fMaxSlots + 1) * sizeof(UInt64), mask);
    if (!fDCBAAMem) return false;
    fDCBAA = (volatile UInt64 *)fDCBAAMem->getBytesNoCopy();
    bzero((void *)fDCBAA, (fMaxSlots + 1) * sizeof(UInt64));
    opWrite64(XHCI_DCBAAP, fDCBAAMem->getPhysicalAddress());

    /* Scratchpad buffers: real xHCI silicon almost always needs some pages
     * of controller-owned working memory (unlike QEMU's emulated xHCI,
     * which never asked for any and let this go unnoticed). If
     * HCSPARAMS2.Max Scratchpad Buffers is nonzero, DCBAA[0] must point at
     * an array of that many page-sized buffers' physical addresses before
     * the controller can process any command; skipping it is exactly why
     * ringing the doorbell for the very first command (Enable Slot) halted
     * the controller with USBSTS.HSE on real hardware while working fine
     * under QEMU. */
    UInt32 hcsp2 = capRead32(XHCI_HCSPARAMS2);
    UInt32 maxScratchHi = (hcsp2 >> 21) & 0x1F;
    UInt32 maxScratchLo = (hcsp2 >> 27) & 0x1F;
    UInt32 maxScratchpadBufs = (maxScratchHi << 5) | maxScratchLo;
    XHCI_Log("HCSPARAMS2=%08x maxScratchpadBufs=%u", hcsp2, maxScratchpadBufs);
    if (maxScratchpadBufs > 32) {
        XHCI_Log("maxScratchpadBufs %u exceeds fixed array size, capping at 32", maxScratchpadBufs);
        maxScratchpadBufs = 32;
    }

    if (maxScratchpadBufs > 0) {
        fScratchpadArrayMem = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
            kernel_task, kIOMemoryPhysicallyContiguous | kIODirectionInOut,
            maxScratchpadBufs * sizeof(UInt64), mask);
        if (!fScratchpadArrayMem) return false;
        volatile UInt64 *scratchArray = (volatile UInt64 *)fScratchpadArrayMem->getBytesNoCopy();
        bzero((void *)scratchArray, maxScratchpadBufs * sizeof(UInt64));

        for (UInt32 i = 0; i < maxScratchpadBufs; i++) {
            IOBufferMemoryDescriptor *buf = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
                kernel_task, kIOMemoryPhysicallyContiguous | kIODirectionInOut, 0x1000, mask);
            if (!buf) return false;
            bzero((void *)buf->getBytesNoCopy(), 0x1000);
            scratchArray[i] = buf->getPhysicalAddress();
            fScratchpadBufMem[i] = buf;
        }
        fDCBAA[0] = fScratchpadArrayMem->getPhysicalAddress();
    }
    return true;
}

bool RavynXHCIPort::allocRing(IOBufferMemoryDescriptor **outMem, volatile XHCITRB **outVirt, UInt32 trbCount)
{
    UInt64 mask = 0xFFFFFFFFFFFFFFFFULL;
    IOBufferMemoryDescriptor *mem = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIOMemoryPhysicallyContiguous | kIODirectionInOut,
        trbCount * sizeof(XHCITRB), mask);
    if (!mem) return false;
    volatile XHCITRB *virt = (volatile XHCITRB *)mem->getBytesNoCopy();
    bzero((void *)virt, trbCount * sizeof(XHCITRB));
    *outMem = mem;
    *outVirt = virt;
    return true;
}

bool RavynXHCIPort::setupCommandRing()
{
    if (!allocRing(&fCmdRingMem, &fCmdRing, kRingTRBs)) return false;
    fCmdRingEnqueue = 0;
    fCmdRingCycle = 1;

    /* Link the last TRB back to TRB 0, toggle cycle. */
    UInt64 base = fCmdRingMem->getPhysicalAddress();
    fCmdRing[kRingTRBs - 1].param = base;
    fCmdRing[kRingTRBs - 1].status = 0;
    fCmdRing[kRingTRBs - 1].control = TRB_SET_TYPE(TRB_TYPE_LINK) | TRB_TC | TRB_CYCLE;

    opWrite64(XHCI_CRCR, (base & ~0xFULL) | XHCI_CRCR_RCS);
    return true;
}

bool RavynXHCIPort::setupEventRing()
{
    if (!allocRing(&fEventRingMem, &fEventRing, kRingTRBs)) return false;
    fEventRingDequeue = 0;
    fEventRingCycle = 1;

    UInt64 mask = 0xFFFFFFFFFFFFFFFFULL;
    fERSTMem = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIOMemoryPhysicallyContiguous | kIODirectionInOut, 16, mask);
    if (!fERSTMem) return false;
    volatile UInt32 *erst = (volatile UInt32 *)fERSTMem->getBytesNoCopy();
    UInt64 ringBase = fEventRingMem->getPhysicalAddress();
    erst[0] = (UInt32)(ringBase & 0xFFFFFFFFU);
    erst[1] = (UInt32)(ringBase >> 32);
    erst[2] = kRingTRBs;
    erst[3] = 0;

    rtWrite32(XHCI_RT_IR0 + XHCI_IR_ERSTSZ, 1);
    rtWrite64(XHCI_RT_IR0 + XHCI_IR_ERDP, ringBase);
    rtWrite64(XHCI_RT_IR0 + XHCI_IR_ERSTBA, fERSTMem->getPhysicalAddress());

    /* IMAN.IE (Interrupter Enable) is only supposed to gate the actual
     * interrupt/MSI signal per spec, not whether the xHC queues events at
     * all, but we're polling (no ISR registered, and USBCMD.INTE stays 0
     * so this can't actually assert a system interrupt line) and it costs
     * nothing to set it in case some real silicon ties event posting to it
     * more strictly than the spec requires. */
    rtWrite32(XHCI_RT_IR0 + XHCI_IR_IMAN, XHCI_IMAN_IE);
    return true;
}

void RavynXHCIPort::pushTRB(volatile XHCITRB *ring, UInt32 &enqueue, UInt8 &cycle, UInt32 ringSizeTRBs,
                            UInt64 param, UInt32 status, UInt32 control)
{
    ring[enqueue].param = param;
    ring[enqueue].status = status;
    ring[enqueue].control = control | (cycle ? TRB_CYCLE : 0);
    enqueue++;
    if (enqueue == ringSizeTRBs - 1) {
        /* LINK TRB sits at ringSizeTRBs-1; toggle our own cycle and loop. */
        ring[enqueue].control = (ring[enqueue].control & ~TRB_CYCLE) | (cycle ? TRB_CYCLE : 0);
        enqueue = 0;
        cycle ^= 1;
    }
}

bool RavynXHCIPort::doCommand(UInt64 param, UInt32 status, UInt32 controlNoCycle,
                              UInt8 *outCC, UInt32 *outSlotId, UInt64 timeoutMs)
{
    IOLockLock(fCmdLock);

    /* Physical address of the TRB we're about to enqueue, so we can confirm
     * a Command Completion Event actually belongs to THIS command (its
     * param field is the Command TRB Pointer) rather than blindly accepting
     * the first completion we see. Without this, a stray late completion
     * for an earlier timed-out command gets misattributed to whichever
     * command asks next - observed on real hardware where two ports with
     * bogus PORTSC speed values timed out on Enable Slot and appeared to
     * poison enumeration of the next (real) port. */
    UInt64 cmdTRBPhys = fCmdRingMem->getPhysicalAddress() + (UInt64)fCmdRingEnqueue * sizeof(XHCITRB);

    pushTRB(fCmdRing, fCmdRingEnqueue, fCmdRingCycle, kRingTRBs, param, status, controlNoCycle);
    ringDoorbell(0, 0);

    bool ok = false;
    for (UInt64 waited = 0; waited < timeoutMs; waited++) {
        UInt64 evParam   = fEventRing[fEventRingDequeue].param;
        UInt32 evControl = fEventRing[fEventRingDequeue].control;
        UInt32 evStatus  = fEventRing[fEventRingDequeue].status;
        if ((evControl & TRB_CYCLE) == (fEventRingCycle ? TRB_CYCLE : 0)) {
            if (TRB_TYPE(evControl) == TRB_TYPE_CMD_COMPLETION && evParam == cmdTRBPhys) {
                if (outCC) *outCC = TRB_CC(evStatus);
                if (outSlotId) *outSlotId = TRB_GET_SLOT(evControl);
                ok = true;
            }
            fEventRingDequeue++;
            if (fEventRingDequeue == kRingTRBs) { fEventRingDequeue = 0; fEventRingCycle ^= 1; }
            /* ERDP.EHB (Event Handler Busy, bit3) must be explicitly written
             * as 1 by software to acknowledge/clear it after consuming an
             * event, not just passed through from a prior read. QEMU's
             * emulated xHCI doesn't seem to enforce this and kept posting
             * events regardless; real hardware can stop posting new events
             * (including the Command Completion Event for Enable Slot)
             * until EHB is cleared, which read like every doCommand() just
             * timing out for no reason. */
            UInt64 newErdp = fEventRingMem->getPhysicalAddress() + fEventRingDequeue * sizeof(XHCITRB);
            rtWrite64(XHCI_RT_IR0 + XHCI_IR_ERDP, (newErdp & ~0xFULL) | XHCI_ERDP_EHB);
            if (ok) break;
            continue;
        }
        IOSleep(1);
    }
    IOLockUnlock(fCmdLock);
    if (!ok) {
        /* Dump enough state to tell apart "doorbell never reached the
         * controller"/"controller halted"/"command ring not running" from
         * "event ring genuinely empty" without another blind guess-and-wait
         * real-hardware round trip. */
        UInt32 usbcmd = opRead32(XHCI_USBCMD);
        UInt32 usbsts = opRead32(XHCI_USBSTS);
        UInt64 crcr = opRead64(XHCI_CRCR);
        UInt64 erdp = rtRead64(XHCI_RT_IR0 + XHCI_IR_ERDP);
        UInt32 iman = rtRead32(XHCI_RT_IR0 + XHCI_IR_IMAN);
        UInt64 evParamDbg   = fEventRing[fEventRingDequeue].param;
        UInt32 evStatusDbg  = fEventRing[fEventRingDequeue].status;
        UInt32 evControlDbg = fEventRing[fEventRingDequeue].control;
        XHCI_Log("doCommand timed out (type=%u) USBCMD=%08x USBSTS=%08x CRCR=%016llx "
                "ERDP=%016llx IMAN=%08x deqIdx=%u expectCycle=%u eventAt[param=%016llx "
                "status=%08x control=%08x]",
                (unsigned)TRB_TYPE(controlNoCycle), usbcmd, usbsts,
                (unsigned long long)crcr, (unsigned long long)erdp, iman,
                fEventRingDequeue, fEventRingCycle,
                (unsigned long long)evParamDbg, evStatusDbg, evControlDbg);
    }
    return ok;
}

bool RavynXHCIPort::waitTransferEvent(UInt32 slotId, UInt32 epDCI, UInt8 *outCC, UInt32 timeoutMs)
{
    for (UInt32 waited = 0; waited < timeoutMs; waited++) {
        UInt32 evControl = fEventRing[fEventRingDequeue].control;
        UInt32 evStatus  = fEventRing[fEventRingDequeue].status;
        if ((evControl & TRB_CYCLE) == (fEventRingCycle ? TRB_CYCLE : 0)) {
            bool match = TRB_TYPE(evControl) == TRB_TYPE_TRANSFER_EVENT &&
                         TRB_GET_SLOT(evControl) == slotId;
            if (match && outCC) *outCC = TRB_CC(evStatus);
            fEventRingDequeue++;
            if (fEventRingDequeue == kRingTRBs) { fEventRingDequeue = 0; fEventRingCycle ^= 1; }
            /* ERDP.EHB (Event Handler Busy, bit3) must be explicitly written
             * as 1 by software to acknowledge/clear it after consuming an
             * event, not just passed through from a prior read. QEMU's
             * emulated xHCI doesn't seem to enforce this and kept posting
             * events regardless; real hardware can stop posting new events
             * (including the Command Completion Event for Enable Slot)
             * until EHB is cleared, which read like every doCommand() just
             * timing out for no reason. */
            UInt64 newErdp = fEventRingMem->getPhysicalAddress() + fEventRingDequeue * sizeof(XHCITRB);
            rtWrite64(XHCI_RT_IR0 + XHCI_IR_ERDP, (newErdp & ~0xFULL) | XHCI_ERDP_EHB);
            if (match) return true;
            continue;
        }
        IOSleep(1);
    }
    (void)epDCI;
    return false;
}

void RavynXHCIPort::scanPorts()
{
    /* Explicitly power every port on. Ports come up powered after HCRST on
     * most real controllers, but not guaranteed, and PP is a normal
     * read-write bit we must set ourselves per spec rather than assume.
     * Give the device a moment to settle onto the bus before the first
     * connect-status read. */
    for (UInt32 p = 0; p < fMaxPorts; p++) {
        UInt32 portsc = portRead32(p, XHCI_PORTSC);
        if (!(portsc & XHCI_PORTSC_PP)) {
            portWrite32(p, XHCI_PORTSC, (portsc & ~(XHCI_PORTSC_RW1CS | XHCI_PORTSC_PR)) | XHCI_PORTSC_PP);
        }
    }
    XHCI_Log("checkpoint after port power-up loop: USBCMD=%08x USBSTS=%08x",
            opRead32(XHCI_USBCMD), opRead32(XHCI_USBSTS));
    IOSleep(20);

    /* Redetect: a device that hasn't finished attaching/negotiating yet
     * won't show CCS on the very first pass. Poll a handful of times with
     * a short delay between rounds instead of a single one-shot scan,
     * matching how real USB stacks handle late-appearing connect events. */
    bool seen[64] = { false };
    for (int pass = 0; pass < 10; pass++) {
        bool anyNew = false;
        for (UInt32 p = 0; p < fMaxPorts && p < 64; p++) {
            if (seen[p]) continue;
            UInt32 portsc = portRead32(p, XHCI_PORTSC);
            if (!(portsc & XHCI_PORTSC_CCS)) continue;
            seen[p] = true;
            anyNew = true;
            XHCI_Log("Port %u connected, portsc=%08x speed=%u", p, portsc, XHCI_PORTSC_SPEED(portsc));
            resetAndEnumeratePort(p);
        }
        if (!anyNew && pass > 0) break;
        IOSleep(50);
    }
}

bool RavynXHCIPort::resetAndEnumeratePort(UInt32 port0based)
{
    UInt32 portsc = portRead32(port0based, XHCI_PORTSC);

    /* Clear any leftover RW1C change bits (CSC/PEC/WRC/OCC/PRC/PLC/CEC) from
     * before we ever touched this port, observed on real hardware:
     * portsc already had CSC+PRC set at the very first read, before we
     * issued our own reset. Writing PR while stale change bits are still
     * set doesn't itself break anything per spec, but it means our post-
     * reset "did this complete" read can't tell a stale PRC from a fresh
     * one, and risks racing/misreading transitional PLS values. Clear
     * first, then issue the reset as a clean, separate write. */
    if (portsc & XHCI_PORTSC_RW1CS) {
        XHCI_Log("Port %u: clearing stale change bits before reset, portsc=%08x",
                port0based, portsc);
        /* portsc & ~XHCI_PORTSC_PR, NOT portsc & XHCI_PORTSC_RW1CS: writing
         * only the RW1C bits back would zero every other field on this
         * register, including PP (Port Power, bit9), which actually
         * powers the port off. Writing the value back almost as-read
         * (RW1C bits set to 1 clear themselves; everything else, PP
         * included, is preserved because we're writing it back exactly
         * as it read) while explicitly zeroing PR so we don't re-trigger
         * a reset is the correct "clear change bits, change nothing else"
         * write. Same fix applies to the post-reset clear below. */
        portWrite32(port0based, XHCI_PORTSC, portsc & ~XHCI_PORTSC_PR);
        portsc = portRead32(port0based, XHCI_PORTSC);
    }

    portWrite32(port0based, XHCI_PORTSC, (portsc & ~XHCI_PORTSC_RW1CS) | XHCI_PORTSC_PR);
    XHCI_Log("Port %u: checkpoint right after PR write: USBCMD=%08x USBSTS=%08x",
            port0based, opRead32(XHCI_USBCMD), opRead32(XHCI_USBSTS));

    bool enabled = false;
    for (int i = 0; i < 1000; i++) {
        portsc = portRead32(port0based, XHCI_PORTSC);
        if (portsc & XHCI_PORTSC_PED) { enabled = true; break; }
        IOSleep(2);
    }
    XHCI_Log("Port %u: checkpoint after enable-poll (enabled=%d): USBCMD=%08x USBSTS=%08x",
            port0based, enabled, opRead32(XHCI_USBCMD), opRead32(XHCI_USBSTS));
    /* Clear change bits (RW1C), preserving PP and everything else. */
    portWrite32(port0based, XHCI_PORTSC, portsc & ~XHCI_PORTSC_PR);

    if (!enabled) {
        XHCI_Log("Port %u reset: never became enabled, portsc=%08x", port0based, portsc);
        return false;
    }

    UInt32 speed = XHCI_PORTSC_SPEED(portRead32(port0based, XHCI_PORTSC));
    XHCI_Log("Port %u enabled, speed=%u", port0based, speed);

    if (speed == 0) {
        /* Invalid/undefined Port Speed ID - we don't parse the xHCI
         * Extended Capabilities' Supported Protocol structures, so PORTSC's
         * raw speed field is unreliable on ports that need that mapping
         * (observed on real hardware: two ports reported speed=1 on connect
         * then speed=0 once enabled). Issuing Enable Slot on a bogus port
         * times out and, worse, appears to leave stale state on the command/
         * event ring that then poisons enumeration of the *next* (real,
         * correctly-speed-reporting) port. Skip rather than risk that.
         */
        XHCI_Log("Port %u: invalid speed, skipping enumeration", port0based);
        return false;
    }

    return tryEnumerateMassStorage(port0based);
}

bool RavynXHCIPort::enableSlot(UInt32 *outSlotId)
{
    UInt8 cc = 0;
    UInt32 slot = 0;
    if (!doCommand(0, 0, TRB_SET_TYPE(TRB_TYPE_ENABLE_SLOT), &cc, &slot)) return false;
    if (cc != TRB_CC_SUCCESS) {
        XHCI_Log("Enable Slot failed cc=%u", cc);
        return false;
    }
    *outSlotId = slot;
    return true;
}

/* Two-phase enumeration, per spec (and every real USB stack): a guessed
 * default max packet size for EP0 based on link speed alone isn't reliable
 * enough to safely send SET_ADDRESS on the wire (observed on real hardware:
 * "Address Device failed cc=4", USB Transaction Error). Phase 1 issues
 * Address Device with BSR (Block Set Address Request) set, which sets up
 * the default control endpoint enough to run control transfers WITHOUT
 * actually sending SET_ADDRESS, so we can safely GET_DESCRIPTOR(device, 8
 * bytes) and learn the device's real bMaxPacketSize0. Phase 2 re-issues
 * Address Device with BSR clear and the corrected max packet size, which
 * is what actually assigns the address. */
bool RavynXHCIPort::addressDevice(UInt32 slotId, UInt32 port0based, UInt32 routeString,
                                  UInt32 speed, UInt16 &maxPacket0)
{
    SlotResources &sr = fSlots[slotId];
    UInt64 mask = 0xFFFFFFFFFFFFFFFFULL;

    sr.deviceCtxMem = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIOMemoryPhysicallyContiguous | kIODirectionInOut,
        sizeof(XHCIDeviceContext), mask);
    sr.inputCtxMem = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIOMemoryPhysicallyContiguous | kIODirectionInOut,
        sizeof(XHCIInputContext), mask);
    if (!sr.deviceCtxMem || !sr.inputCtxMem) return false;
    bzero((void*)sr.deviceCtxMem->getBytesNoCopy(), sizeof(XHCIDeviceContext));
    bzero((void*)sr.inputCtxMem->getBytesNoCopy(), sizeof(XHCIInputContext));

    if (!allocRing(&sr.ep0RingMem, &sr.ep0Ring, kRingTRBs)) return false;
    sr.ep0Enqueue = 0;
    sr.ep0Cycle = 1;
    {
        UInt64 base = sr.ep0RingMem->getPhysicalAddress();
        sr.ep0Ring[kRingTRBs - 1].param = base;
        sr.ep0Ring[kRingTRBs - 1].control = TRB_SET_TYPE(TRB_TYPE_LINK) | TRB_TC | TRB_CYCLE;
    }

    fDCBAA[slotId] = sr.deviceCtxMem->getPhysicalAddress();

    /* Speed -> conservative default max packet size for EP0 (USB2 spec
     * table), used only for the BSR probe transfer itself; the real value
     * comes from the device's own device descriptor. */
    UInt16 defaultMaxPkt = 8;
    switch (speed) {
        case 1: defaultMaxPkt = 8;   break; /* full speed */
        case 2: defaultMaxPkt = 8;   break; /* low speed */
        case 3: defaultMaxPkt = 64;  break; /* high speed */
        case 4: defaultMaxPkt = 512; break; /* super speed */
        default: defaultMaxPkt = 8;  break;
    }

    if (!sendAddressDeviceCommand(slotId, port0based, routeString, speed, defaultMaxPkt, true))
        return false;

    USBDeviceDescriptor devDesc8;
    bzero(&devDesc8, sizeof(devDesc8));
    USBSetupPacket getDevDesc8 = { 0x80, USB_REQ_GET_DESCRIPTOR,
                                  (UInt16)(USB_DESC_DEVICE << 8), 0, 8 };
    UInt16 realMaxPkt = defaultMaxPkt;
    if (controlTransfer(slotId, getDevDesc8, &devDesc8, 8, true) && devDesc8.bMaxPacketSize0 > 0) {
        realMaxPkt = (speed == 4) ? (UInt16)(1U << devDesc8.bMaxPacketSize0) /* SS encodes as 2^n */
                                  : devDesc8.bMaxPacketSize0;
    } else {
        XHCI_Log("Port %u slot %u: BSR probe GET_DESCRIPTOR(8) failed, keeping default maxpkt=%u",
                port0based, slotId, defaultMaxPkt);
    }

    maxPacket0 = realMaxPkt;
    return sendAddressDeviceCommand(slotId, port0based, routeString, speed, realMaxPkt, false);
}

bool RavynXHCIPort::sendAddressDeviceCommand(UInt32 slotId, UInt32 port0based, UInt32 routeString,
                                             UInt32 speed, UInt16 maxPkt, bool bsr)
{
    SlotResources &sr = fSlots[slotId];
    XHCIInputContext *ic = (XHCIInputContext *)sr.inputCtxMem->getBytesNoCopy();
    bzero(ic, sizeof(*ic));
    ic->control.dropFlags = 0;
    ic->control.addFlags = (1U << 0) /* slot ctx */ | (1U << 1) /* EP0 ctx */;

    ic->slot.dword0 = (1U << SLOT_CTX_ENTRIES_SHIFT) | (speed << SLOT_CTX_SPEED_SHIFT);
    ic->slot.dword1 = ((port0based + 1) << SLOT_CTX_ROOTPORT_SHIFT);
    ic->slot.dword2 = routeString;
    ic->slot.dword3 = 0;

    ic->ep[0].dword0 = 0;
    ic->ep[0].dword1 = (EP_TYPE_CONTROL << EP_CTX_TYPE_SHIFT) | ((UInt32)maxPkt << EP_CTX_MAXPKT_SHIFT);
    ic->ep[0].trDequeuePtr = sr.ep0RingMem->getPhysicalAddress() | 1 /* DCS */;
    ic->ep[0].avgTrbLen_maxEsitLo = 8;

    UInt8 cc = 0;
    UInt32 slotOut = 0;
    UInt32 control = TRB_SET_TYPE(TRB_TYPE_ADDRESS_DEVICE) | TRB_SET_SLOT(slotId);
    if (bsr) control |= TRB_ADDR_DEV_BSR;
    bool ok = doCommand(sr.inputCtxMem->getPhysicalAddress(), 0, control, &cc, &slotOut, 1000);
    if (!ok || cc != TRB_CC_SUCCESS) {
        XHCI_Log("Address Device (bsr=%d) failed slot=%u cc=%u", bsr, slotId, cc);
        return false;
    }
    return true;
}

bool RavynXHCIPort::controlTransfer(UInt32 slotId, const USBSetupPacket &setup,
                                    void *buf, UInt16 len, bool in)
{
    SlotResources &sr = fSlots[slotId];

    /* Setup stage: 8 bytes of the setup packet go directly in TRB.param (IDT). */
    UInt64 setupParam;
    bcopy(&setup, &setupParam, sizeof(setupParam));
    UInt32 trt = (len == 0) ? TRB_SETUP_TRT_NO_DATA : (in ? TRB_SETUP_TRT_IN_DATA : TRB_SETUP_TRT_OUT_DATA);
    pushTRB(sr.ep0Ring, sr.ep0Enqueue, sr.ep0Cycle, kRingTRBs,
           setupParam, 8, TRB_SET_TYPE(TRB_TYPE_SETUP_STAGE) | TRB_IDT | (trt << TRB_SETUP_TRT_SHIFT));

    IOBufferMemoryDescriptor *xferMem = NULL;
    if (len > 0) {
        UInt64 mask = 0xFFFFFFFFFFFFFFFFULL;
        xferMem = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
            kernel_task, kIOMemoryPhysicallyContiguous | kIODirectionInOut, len, mask);
        if (!xferMem) return false;
        if (!in) bcopy(buf, xferMem->getBytesNoCopy(), len);
        else bzero(xferMem->getBytesNoCopy(), len);

        pushTRB(sr.ep0Ring, sr.ep0Enqueue, sr.ep0Cycle, kRingTRBs,
               xferMem->getPhysicalAddress(), len,
               TRB_SET_TYPE(TRB_TYPE_DATA_STAGE) | (in ? TRB_DIR_IN : 0));
    }

    /* Status stage direction is opposite of data direction (or IN if no data). */
    UInt32 statusDir = (len > 0 && !in) ? TRB_DIR_IN : (len > 0 ? 0 : TRB_DIR_IN);
    pushTRB(sr.ep0Ring, sr.ep0Enqueue, sr.ep0Cycle, kRingTRBs,
           0, 0, TRB_SET_TYPE(TRB_TYPE_STATUS_STAGE) | statusDir | TRB_IOC);

    ringDoorbell(slotId, XHCI_DB_TARGET_CONTROL_EP0);

    UInt8 cc = 0;
    bool ok = waitTransferEvent(slotId, 1, &cc, 1000);
    if (ok && in && buf && len > 0 && xferMem)
        bcopy(xferMem->getBytesNoCopy(), buf, len);
    if (xferMem) xferMem->release();

    if (!ok || (cc != TRB_CC_SUCCESS && cc != TRB_CC_SHORT_PACKET)) {
        XHCI_Log("control transfer failed slot=%u req=%u cc=%u ok=%d", slotId, setup.bRequest, cc, ok);
        return false;
    }
    return true;
}

bool RavynXHCIPort::configureBulkEndpoints(UInt32 slotId, UInt8 inEp, UInt16 inMaxPkt,
                                           UInt8 outEp, UInt16 outMaxPkt)
{
    SlotResources &sr = fSlots[slotId];
    XHCIInputContext *ic = (XHCIInputContext *)sr.inputCtxMem->getBytesNoCopy();
    bzero(ic, sizeof(*ic));

    /* DCI = endpoint number * 2 + (dir_in ? 1 : 0); index into ep[] is DCI - 1. */
    UInt32 inDCI = inEp * 2 + 1;
    UInt32 outDCI = outEp * 2;
    UInt32 maxDCI = (inDCI > outDCI) ? inDCI : outDCI;

    ic->control.addFlags = (1U << 0) /* slot */ | (1U << inDCI) | (1U << outDCI);

    /* Copy current slot context forward, bump context entries. */
    XHCIDeviceContext *dc = (XHCIDeviceContext *)sr.deviceCtxMem->getBytesNoCopy();
    ic->slot = dc->slot;
    ic->slot.dword0 = (ic->slot.dword0 & ~((UInt32)0x1F << SLOT_CTX_ENTRIES_SHIFT)) |
                      (maxDCI << SLOT_CTX_ENTRIES_SHIFT);

    if (!allocRing(&sr.bulkInRingMem, &sr.bulkInRing, kRingTRBs)) return false;
    sr.bulkInEnqueue = 0; sr.bulkInCycle = 1;
    sr.bulkInRing[kRingTRBs - 1].param = sr.bulkInRingMem->getPhysicalAddress();
    sr.bulkInRing[kRingTRBs - 1].control = TRB_SET_TYPE(TRB_TYPE_LINK) | TRB_TC | TRB_CYCLE;

    if (!allocRing(&sr.bulkOutRingMem, &sr.bulkOutRing, kRingTRBs)) return false;
    sr.bulkOutEnqueue = 0; sr.bulkOutCycle = 1;
    sr.bulkOutRing[kRingTRBs - 1].param = sr.bulkOutRingMem->getPhysicalAddress();
    sr.bulkOutRing[kRingTRBs - 1].control = TRB_SET_TYPE(TRB_TYPE_LINK) | TRB_TC | TRB_CYCLE;

    ic->ep[inDCI - 1].dword1 = (EP_TYPE_BULK_IN << EP_CTX_TYPE_SHIFT) | ((UInt32)inMaxPkt << EP_CTX_MAXPKT_SHIFT);
    ic->ep[inDCI - 1].trDequeuePtr = sr.bulkInRingMem->getPhysicalAddress() | 1;
    ic->ep[inDCI - 1].avgTrbLen_maxEsitLo = inMaxPkt;

    ic->ep[outDCI - 1].dword1 = (EP_TYPE_BULK_OUT << EP_CTX_TYPE_SHIFT) | ((UInt32)outMaxPkt << EP_CTX_MAXPKT_SHIFT);
    ic->ep[outDCI - 1].trDequeuePtr = sr.bulkOutRingMem->getPhysicalAddress() | 1;
    ic->ep[outDCI - 1].avgTrbLen_maxEsitLo = outMaxPkt;

    UInt8 cc = 0;
    UInt32 slotOut = 0;
    bool ok = doCommand(sr.inputCtxMem->getPhysicalAddress(), 0,
                        TRB_SET_TYPE(TRB_TYPE_CONFIGURE_EP) | TRB_SET_SLOT(slotId),
                        &cc, &slotOut, 1000);
    if (!ok || cc != TRB_CC_SUCCESS) {
        XHCI_Log("Configure Endpoint failed slot=%u cc=%u", slotId, cc);
        return false;
    }
    return true;
}

/* A Normal TRB's Transfer Length is only a 17-bit field in TRB.status
 * (bits 22-31 there are TD Size / Interrupter Target, not length) so any
 * single TRB is capped at 0x1FFFF bytes, and anything above ~64KB risks
 * hardware-specific quirks. Chain multiple TRBs for larger buffers: all but
 * the last get TRB_CH (chain), only the last gets TRB_IOC, and the xHC
 * reports one Transfer Event for the whole chained TD (matching the single
 * waitTransferEvent() call below). Without this, a large HFS pagein read
 * silently overflowed the length field and corrupted the whole transfer
 * (observed as garbage CSW signature bytes - literal code bytes from
 * unrelated memory - and a kernel panic soon after). */
#define kMaxTRBTransferBytes 0x10000U

bool RavynXHCIPort::bulkTransfer(UInt32 slotId, UInt8 epNum, bool in,
                                 IOBufferMemoryDescriptor *xferMem, UInt32 len, UInt32 timeoutMs)
{
    SlotResources &sr = fSlots[slotId];
    volatile XHCITRB *ring = in ? sr.bulkInRing : sr.bulkOutRing;
    UInt32 &enqueue = in ? sr.bulkInEnqueue : sr.bulkOutEnqueue;
    UInt8 &cycle = in ? sr.bulkInCycle : sr.bulkOutCycle;
    UInt32 dci = in ? (epNum * 2 + 1) : (epNum * 2);
    UInt64 base = xferMem->getPhysicalAddress();

    UInt32 remaining = len ? len : 0;
    UInt32 offset = 0;
    do {
        UInt32 chunk = remaining;
        if (chunk > kMaxTRBTransferBytes) chunk = kMaxTRBTransferBytes;
        bool isLast = (offset + chunk >= len);
        pushTRB(ring, enqueue, cycle, kRingTRBs,
               base + offset, chunk,
               TRB_SET_TYPE(TRB_TYPE_NORMAL) | (isLast ? TRB_IOC : TRB_CH));
        offset += chunk;
        remaining -= chunk;
    } while (remaining > 0);
    ringDoorbell(slotId, dci);

    UInt8 cc = 0;
    if (!waitTransferEvent(slotId, dci, &cc, timeoutMs)) {
        XHCI_Log("bulk transfer timed out slot=%u ep=%u in=%d len=%u", slotId, epNum, in, len);
        return false;
    }
    if (cc != TRB_CC_SUCCESS && cc != TRB_CC_SHORT_PACKET) {
        XHCI_Log("bulk transfer failed slot=%u ep=%u in=%d len=%u cc=%u", slotId, epNum, in, len, cc);
        return false;
    }
    return true;
}

/* Full enumeration: Enable Slot -> Address -> GET_DESCRIPTOR -> find MSC iface -> Configure EP */
bool RavynXHCIPort::tryEnumerateMassStorage(UInt32 port0based)
{
    UInt32 speed = XHCI_PORTSC_SPEED(portRead32(port0based, XHCI_PORTSC));

    UInt32 slotId = 0;
    if (!enableSlot(&slotId)) return false;
    XHCI_Log("Port %u: slot %u enabled", port0based, slotId);

    UInt16 maxPkt0 = 8;
    if (!addressDevice(slotId, port0based, 0, speed, maxPkt0)) return false;

    USBDeviceDescriptor devDesc;
    bzero(&devDesc, sizeof(devDesc));
    USBSetupPacket getDevDesc = { 0x80, USB_REQ_GET_DESCRIPTOR,
                                  (UInt16)(USB_DESC_DEVICE << 8), 0, sizeof(devDesc) };
    if (!controlTransfer(slotId, getDevDesc, &devDesc, sizeof(devDesc), true)) return false;
    XHCI_Log("Port %u slot %u: device class=%02x subclass=%02x proto=%02x vid=%04x pid=%04x cfgs=%u",
            port0based, slotId, devDesc.bDeviceClass, devDesc.bDeviceSubClass,
            devDesc.bDeviceProtocol, devDesc.idVendor, devDesc.idProduct, devDesc.bNumConfigurations);

    if (devDesc.bNumConfigurations == 0) return false;

    /* Read config descriptor header first to learn wTotalLength, then re-read full blob. */
    USBConfigDescriptor cfgHdr;
    bzero(&cfgHdr, sizeof(cfgHdr));
    USBSetupPacket getCfgHdr = { 0x80, USB_REQ_GET_DESCRIPTOR,
                                (UInt16)(USB_DESC_CONFIGURATION << 8), 0, sizeof(cfgHdr) };
    if (!controlTransfer(slotId, getCfgHdr, &cfgHdr, sizeof(cfgHdr), true)) return false;

    UInt16 totalLen = cfgHdr.wTotalLength;
    if (totalLen < sizeof(cfgHdr) || totalLen > 512) return false;

    uint8_t *cfgBuf = (uint8_t *)IOMalloc(totalLen);
    if (!cfgBuf) return false;
    USBSetupPacket getCfgFull = { 0x80, USB_REQ_GET_DESCRIPTOR,
                                 (UInt16)(USB_DESC_CONFIGURATION << 8), 0, totalLen };
    if (!controlTransfer(slotId, getCfgFull, cfgBuf, totalLen, true)) { IOFree(cfgBuf, totalLen); return false; }

    /* Walk descriptors for a Mass Storage / SCSI / Bulk-Only interface + its bulk endpoints. */
    UInt8 cfgValue = cfgHdr.bConfigurationValue;
    bool foundMSC = false;
    UInt8 bulkInEp = 0, bulkOutEp = 0;
    UInt16 bulkInMaxPkt = 0, bulkOutMaxPkt = 0;

    UInt32 off = 0;
    bool inMSCInterface = false;
    while (off + 2 <= totalLen) {
        UInt8 len = cfgBuf[off];
        UInt8 type = cfgBuf[off + 1];
        if (len < 2 || off + len > totalLen) break;

        if (type == 4 /* interface */ && len >= sizeof(USBInterfaceDescriptor)) {
            USBInterfaceDescriptor *ifd = (USBInterfaceDescriptor *)(cfgBuf + off);
            inMSCInterface = (ifd->bInterfaceClass == USB_IF_CLASS_MASS_STORAGE &&
                              ifd->bInterfaceSubClass == USB_IF_SUBCLASS_SCSI &&
                              ifd->bInterfaceProtocol == USB_IF_PROTOCOL_BULK_ONLY);
            if (inMSCInterface) foundMSC = true;
        } else if (type == 5 /* endpoint */ && inMSCInterface && len >= sizeof(USBEndpointDescriptor)) {
            USBEndpointDescriptor *epd = (USBEndpointDescriptor *)(cfgBuf + off);
            if ((epd->bmAttributes & USB_EP_TYPE_MASK) == USB_EP_TYPE_BULK) {
                if (epd->bEndpointAddress & USB_EP_DIR_IN) {
                    bulkInEp = epd->bEndpointAddress & USB_EP_ADDR_MASK;
                    bulkInMaxPkt = epd->wMaxPacketSize;
                } else {
                    bulkOutEp = epd->bEndpointAddress & USB_EP_ADDR_MASK;
                    bulkOutMaxPkt = epd->wMaxPacketSize;
                }
            }
        }
        off += len;
    }
    IOFree(cfgBuf, totalLen);

    if (!foundMSC || !bulkInEp || !bulkOutEp) {
        XHCI_Log("Port %u slot %u: no bulk-only mass storage interface found", port0based, slotId);
        return false;
    }
    XHCI_Log("Port %u slot %u: MSC interface found, bulkIn=ep%u(%u) bulkOut=ep%u(%u)",
            port0based, slotId, bulkInEp, bulkInMaxPkt, bulkOutEp, bulkOutMaxPkt);

    USBSetupPacket setCfg = { 0x00, USB_REQ_SET_CONFIGURATION, cfgValue, 0, 0 };
    if (!controlTransfer(slotId, setCfg, NULL, 0, false)) return false;

    if (!configureBulkEndpoints(slotId, bulkInEp, bulkInMaxPkt, bulkOutEp, bulkOutMaxPkt))
        return false;

    /* Record + publish nub; RavynXHCIMassStorageDisk fills capacity via SCSI INQUIRY/READ CAPACITY. */
    int idx = -1;
    for (int i = 0; i < 16; i++) if (!fMSC[i].valid) { idx = i; break; }
    if (idx < 0) return false;

    MSCDevice &m = fMSC[idx];
    bzero(&m, sizeof(m));
    m.valid = true;
    m.slotId = slotId;
    m.portNum = (UInt8)port0based;
    m.bulkInEp = bulkInEp;
    m.bulkOutEp = bulkOutEp;
    m.bulkMaxPacket = bulkInMaxPkt;

    RavynXHCIMassStorageDisk *disk = new RavynXHCIMassStorageDisk;
    if (!disk) return false;
    if (!disk->initWithPort(this, idx) || !disk->attach(this)) {
        disk->release();
        return false;
    }
    fDiskNubs[idx] = disk;
    if (!disk->start(this)) {
        XHCI_Log("Port %u slot %u: nub start() failed", port0based, slotId);
        disk->detach(this);
        disk->release();
        fDiskNubs[idx] = NULL;
        return false;
    }
    disk->registerService();
    return true;
}

IOReturn RavynXHCIPort::botTransfer(UInt32 slotId,
                                    const void *cbwCB, UInt8 cbwLen,
                                    UInt32 dataLen, bool dataIn,
                                    IOMemoryDescriptor *buffer, UInt64 bufOff)
{
    int idx = -1;
    for (int i = 0; i < 16; i++) if (fMSC[i].valid && fMSC[i].slotId == slotId) { idx = i; break; }
    if (idx < 0) return kIOReturnNoDevice;
    MSCDevice &m = fMSC[idx];

    static UInt32 tag = 1;
    UInt64 mask = 0xFFFFFFFFFFFFFFFFULL;

    IOBufferMemoryDescriptor *cbwMem = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIOMemoryPhysicallyContiguous | kIODirectionInOut, sizeof(USBBOTCommandBlockWrapper), mask);
    if (!cbwMem) return kIOReturnNoMemory;
    USBBOTCommandBlockWrapper *cbw = (USBBOTCommandBlockWrapper *)cbwMem->getBytesNoCopy();
    bzero(cbw, sizeof(*cbw));
    cbw->dCBWSignature = BOT_CBW_SIGNATURE;
    cbw->dCBWTag = tag++;
    cbw->dCBWDataTransferLength = dataLen;
    cbw->bmCBWFlags = dataIn ? 0x80 : 0x00;
    cbw->bCBWLUN = 0;
    cbw->bCBWCBLength = cbwLen;
    bcopy(cbwCB, cbw->CBWCB, cbwLen);

    bool ok = bulkTransfer(slotId, m.bulkOutEp, false, cbwMem, sizeof(*cbw), 2000);

    if (ok && dataLen > 0) {
        IOBufferMemoryDescriptor *xferMem = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
            kernel_task, kIOMemoryPhysicallyContiguous | kIODirectionInOut, dataLen, mask);
        if (!xferMem) { cbwMem->release(); return kIOReturnNoMemory; }
        if (!dataIn && buffer) buffer->readBytes(bufOff, xferMem->getBytesNoCopy(), dataLen);

        ok = bulkTransfer(slotId, dataIn ? m.bulkInEp : m.bulkOutEp, dataIn, xferMem, dataLen, 5000);

        if (ok && dataIn && buffer) buffer->writeBytes(bufOff, xferMem->getBytesNoCopy(), dataLen);
        xferMem->release();
    }

    bool statusOk = false;
    if (ok) {
        IOBufferMemoryDescriptor *cswMem = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
            kernel_task, kIOMemoryPhysicallyContiguous | kIODirectionInOut, sizeof(USBBOTCommandStatusWrapper), mask);
        if (cswMem) {
            bzero(cswMem->getBytesNoCopy(), sizeof(USBBOTCommandStatusWrapper));
            if (bulkTransfer(slotId, m.bulkInEp, true, cswMem, sizeof(USBBOTCommandStatusWrapper), 2000)) {
                USBBOTCommandStatusWrapper *csw = (USBBOTCommandStatusWrapper *)cswMem->getBytesNoCopy();
                statusOk = (csw->dCSWSignature == BOT_CSW_SIGNATURE && csw->bCSWStatus == 0);
                if (!statusOk)
                    XHCI_Log("BOT CSW sig=%08x tag=%08x status=%u", csw->dCSWSignature, csw->dCSWTag, csw->bCSWStatus);
            }
            cswMem->release();
        }
    }

    cbwMem->release();
    return statusOk ? kIOReturnSuccess : kIOReturnIOError;
}
