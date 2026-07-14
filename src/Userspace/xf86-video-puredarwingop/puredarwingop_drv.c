/*
 * xf86-video-puredarwingop: minimal Xorg video driver for PureDarwin's
 * IOGOPFramebuffer.
 *
 * This is a busless (no PCI/platform bus) framebuffer driver. It uses PDGOP
 * (the userspace IOKit client) to open the IOGOPFramebuffer user client, read
 * the current mode geometry, and map the linear VRAM aperture. That mapping is
 * handed straight to the fb layer via fbScreenInit, so all of X's rendering
 * lands in real GOP video memory - the same buffer the fbtri triangle demo
 * draws into, now driven by a full Xorg server.
 *
 * Modeled on the stock xf86-video-dummy driver (which is likewise busless),
 * with the malloc'd shadow framebuffer replaced by the PDGOP VRAM mapping.
 */
#include "xf86.h"
#include "xf86_OSproc.h"
#include "xf86str.h"
#include "xf86Module.h"
#include "fb.h"
#include "micmap.h"
#include "mipointer.h"
#include "colormapst.h"
#include "xf86cmap.h"

#include <PDGOP.h>

#define PDGOP_NAME        "puredarwingop"
#define PDGOP_DRIVER_NAME "puredarwingop"
#define PDGOP_VERSION     1000
#define PDGOP_MAJOR       1
#define PDGOP_MINOR       0
#define PDGOP_PATCH       0

/* Per-screen private state. */
typedef struct {
    PDGOPFramebuffer fb;          /* live IOGOPFramebuffer connection + mapping */
    Bool             fbOpen;
    CloseScreenProcPtr CloseScreen;
    OptionInfoPtr    Options;
} PDGOPRec, *PDGOPPtr;

static PDGOPPtr
PDGOPGetRec(ScrnInfoPtr pScrn)
{
    if (pScrn->driverPrivate == NULL) {
        pScrn->driverPrivate = xnfcalloc(sizeof(PDGOPRec), 1);
    }
    return (PDGOPPtr)pScrn->driverPrivate;
}

static void
PDGOPFreeRec(ScrnInfoPtr pScrn)
{
    PDGOPPtr p = pScrn->driverPrivate;

    if (p == NULL) {
        return;
    }
    if (p->fbOpen) {
        PDGOPClose(&p->fb);
        p->fbOpen = FALSE;
    }
    free(p);
    pScrn->driverPrivate = NULL;
}

/* --- forward decls --- */
static const OptionInfoRec *PDGOPAvailableOptions(int chipid, int busid);
static void PDGOPIdentify(int flags);
static Bool PDGOPProbe(DriverPtr drv, int flags);
static Bool PDGOPPreInit(ScrnInfoPtr pScrn, int flags);
static Bool PDGOPScreenInit(ScreenPtr pScreen, int argc, char **argv);
static Bool PDGOPEnterVT(ScrnInfoPtr pScrn);
static void PDGOPLeaveVT(ScrnInfoPtr pScrn);
static Bool PDGOPSwitchMode(ScrnInfoPtr pScrn, DisplayModePtr mode);
static void PDGOPAdjustFrame(ScrnInfoPtr pScrn, int x, int y);
static Bool PDGOPCloseScreen(ScreenPtr pScreen);
static ModeStatus PDGOPValidMode(ScrnInfoPtr pScrn, DisplayModePtr mode,
                                 Bool verbose, int flags);

enum { OPTION_NONE = -1 };

static const OptionInfoRec PDGOPOptions[] = {
    { -1, NULL, OPTV_NONE, {0}, FALSE }
};

/* Field order per DriverRec (xf86str.h): driverVersion, driverName, Identify,
 * Probe, AvailableOptions, module, refCount, driverFunc, supported_devices,
 * PciProbe, platformProbe. Busless driver: no PCI/platform probe. */
_X_EXPORT DriverRec PUREDARWINGOP = {
    PDGOP_VERSION,
    PDGOP_DRIVER_NAME,
    PDGOPIdentify,
    PDGOPProbe,
    PDGOPAvailableOptions,
    NULL,   /* module */
    0,      /* refCount */
    NULL,   /* driverFunc */
    NULL,   /* supported_devices */
    NULL,   /* PciProbe */
    NULL    /* platformProbe */
};

/* Chipset "PCI" id table is irrelevant (busless); a single named chipset. */
static SymTabRec PDGOPChipsets[] = {
    { 0, "puredarwingop" },
    { -1, NULL }
};

/* --- module glue --- */
static MODULESETUPPROTO(PDGOPSetup);

static XF86ModuleVersionInfo PDGOPVersRec = {
    PDGOP_DRIVER_NAME,
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    PDGOP_MAJOR, PDGOP_MINOR, PDGOP_PATCH,
    ABI_CLASS_VIDEODRV,
    ABI_VIDEODRV_VERSION,
    MOD_CLASS_VIDEODRV,
    {0, 0, 0, 0}
};

_X_EXPORT XF86ModuleData puredarwingopModuleData = {
    &PDGOPVersRec,
    PDGOPSetup,
    NULL
};

static void *
PDGOPSetup(void *module, void *opts, int *errmaj, int *errmin)
{
    static Bool initialized = FALSE;

    (void)opts;
    (void)errmin;
    if (!initialized) {
        initialized = TRUE;
        /* Flags 0, NOT HaveDriverFuncs: that flag promises DriverRec.driverFunc
         * is valid, and this driver does not implement one (it is busless - no
         * GET_REQUIRED_HW_INTERFACES etc). Claiming it left Xorg calling through
         * a NULL driverFunc. */
        xf86AddDriver(&PUREDARWINGOP, module, 0);
        return (void *)1;
    }
    if (errmaj) {
        *errmaj = LDR_ONCEONLY;
    }
    return NULL;
}

static const OptionInfoRec *
PDGOPAvailableOptions(int chipid, int busid)
{
    (void)chipid;
    (void)busid;
    return PDGOPOptions;
}

static void
PDGOPIdentify(int flags)
{
    (void)flags;
    xf86PrintChipsets(PDGOP_NAME, "Driver for PureDarwin IOGOPFramebuffer",
                      PDGOPChipsets);
}

static Bool
PDGOPProbe(DriverPtr drv, int flags)
{
    GDevPtr *devSections;
    int      numDevSections;
    int      i;
    Bool     foundScreen = FALSE;

    xf86Msg(X_INFO, "puredarwingop: Probe(flags=0x%x)\n", flags);

    if (flags & PROBE_DETECT) {
        return FALSE;
    }

    numDevSections = xf86MatchDevice(PDGOP_DRIVER_NAME, &devSections);
    xf86Msg(X_INFO, "puredarwingop: xf86MatchDevice -> %d section(s)\n",
            numDevSections);
    if (numDevSections <= 0) {
        return FALSE;
    }

    for (i = 0; i < numDevSections; i++) {
        ScrnInfoPtr pScrn;
        int entity;

        /* Busless: claim a slot with no bus backing. */
        entity = xf86ClaimNoSlot(drv, 0, devSections[i], TRUE);
        pScrn = xf86AllocateScreen(drv, 0);
        if (pScrn == NULL) {
            continue;
        }
        xf86AddEntityToScreen(pScrn, entity);

        pScrn->driverVersion = PDGOP_VERSION;
        pScrn->driverName    = PDGOP_DRIVER_NAME;
        pScrn->name          = PDGOP_NAME;
        pScrn->Probe         = PDGOPProbe;
        pScrn->PreInit       = PDGOPPreInit;
        pScrn->ScreenInit    = PDGOPScreenInit;
        pScrn->SwitchMode    = PDGOPSwitchMode;
        pScrn->AdjustFrame   = PDGOPAdjustFrame;
        pScrn->EnterVT       = PDGOPEnterVT;
        pScrn->LeaveVT       = PDGOPLeaveVT;
        pScrn->ValidMode     = PDGOPValidMode;

        foundScreen = TRUE;
    }

    free(devSections);
    xf86Msg(X_INFO, "puredarwingop: Probe -> %s\n",
            foundScreen ? "found screen" : "no screen");
    return foundScreen;
}

static Bool
PDGOPPreInit(ScrnInfoPtr pScrn, int flags)
{
    PDGOPPtr      p;
    kern_return_t kr;
    DisplayModePtr mode;
    rgb            defaultWeight = { 0, 0, 0 };
    Gamma          zeros = { 0.0, 0.0, 0.0 };

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "PreInit(flags=0x%x, numEntities=%d)\n",
               flags, pScrn->numEntities);

    if (flags & PROBE_DETECT) {
        return FALSE;
    }
    if (pScrn->numEntities != 1) {
        return FALSE;
    }

    p = PDGOPGetRec(pScrn);

    /* Open the IOGOPFramebuffer user client and read its geometry now, so mode
     * setup below reflects the real GOP resolution rather than a guess. */
    kr = PDGOPOpen(&p->fb);
    if (kr != KERN_SUCCESS) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "PDGOPOpen failed at %s: 0x%x\n",
                   PDGOPLastErrorStage(), kr);
        return FALSE;
    }
    p->fbOpen = TRUE;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "IOGOPFramebuffer: %ux%u, %u bpp, stride %u\n",
               p->fb.width, p->fb.height, p->fb.bpp, p->fb.stride);

    pScrn->monitor = pScrn->confScreen->monitor;

    /* GOP is 32bpp BGRA/XRGB; advertise depth 24 in a 32-bit framebuffer. */
    if (!xf86SetDepthBpp(pScrn, 24, 0, p->fb.bpp,
                         Support32bppFb)) {
        return FALSE;
    }
    xf86PrintDepthBpp(pScrn);

    if (!xf86SetWeight(pScrn, defaultWeight, defaultWeight)) {
        return FALSE;
    }
    if (!xf86SetDefaultVisual(pScrn, -1)) {
        return FALSE;
    }

    pScrn->progClock = TRUE;
    pScrn->rgbBits   = 8;
    pScrn->chipset   = PDGOP_DRIVER_NAME;
    pScrn->videoRam  = (int)(p->fb.size / 1024);

    xf86CollectOptions(pScrn, NULL);

    /* Build a single mode matching the live GOP resolution. */
    mode = xnfcalloc(sizeof(DisplayModeRec), 1);
    mode->name        = "GOPCurrent";
    mode->type        = M_T_DRIVER | M_T_PREFERRED;
    mode->HDisplay    = p->fb.width;
    mode->HSyncStart  = p->fb.width;
    mode->HSyncEnd    = p->fb.width;
    mode->HTotal      = p->fb.width;
    mode->VDisplay    = p->fb.height;
    mode->VSyncStart  = p->fb.height;
    mode->VSyncEnd    = p->fb.height;
    mode->VTotal      = p->fb.height;
    mode->Clock       = p->fb.width * p->fb.height * 60 / 1000;
    mode->CrtcHDisplay = mode->HDisplay;
    mode->CrtcVDisplay = mode->VDisplay;
    mode->next = mode;
    mode->prev = mode;

    pScrn->modes       = mode;
    pScrn->currentMode = mode;
    pScrn->virtualX    = p->fb.width;
    pScrn->virtualY    = p->fb.height;
    pScrn->displayWidth = p->fb.stride / (p->fb.bpp / 8);

    /* Physical size unknown; use 96 DPI. */
    pScrn->xDpi = 96;
    pScrn->yDpi = 96;

    xf86SetGamma(pScrn, zeros);

    if (!xf86LoadSubModule(pScrn, "fb")) {
        return FALSE;
    }

    return TRUE;
}

static Bool
PDGOPScreenInit(ScreenPtr pScreen, int argc, char **argv)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    PDGOPPtr    p = PDGOPGetRec(pScrn);
    void       *fbstart;

    (void)argc;
    (void)argv;

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "ScreenInit: fbOpen=%d vram=0x%llx size=0x%llx %dx%d stride=%d bpp=%d\n",
               (int)p->fbOpen, (unsigned long long)p->fb.address,
               (unsigned long long)p->fb.size, pScrn->virtualX, pScrn->virtualY,
               pScrn->displayWidth, pScrn->bitsPerPixel);

    if (!p->fbOpen || p->fb.address == 0) {
        return FALSE;
    }
    fbstart = (void *)(uintptr_t)p->fb.address;

    /* Clear VRAM to black before X takes over. */
    memset(fbstart, 0, (size_t)p->fb.size);

    miClearVisualTypes();
    if (!miSetVisualTypes(pScrn->depth, miGetDefaultVisualMask(pScrn->depth),
                          pScrn->rgbBits, pScrn->defaultVisual)) {
        return FALSE;
    }
    if (!miSetPixmapDepths()) {
        return FALSE;
    }

    if (!fbScreenInit(pScreen, fbstart,
                      pScrn->virtualX, pScrn->virtualY,
                      pScrn->xDpi, pScrn->yDpi,
                      pScrn->displayWidth, pScrn->bitsPerPixel)) {
        return FALSE;
    }

    /* Fix up RGB ordering. */
    {
        VisualPtr visual = pScreen->visuals + pScreen->numVisuals;
        while (--visual >= pScreen->visuals) {
            if ((visual->class | DynamicClass) == DirectColor) {
                visual->offsetRed   = pScrn->offset.red;
                visual->offsetGreen = pScrn->offset.green;
                visual->offsetBlue  = pScrn->offset.blue;
                visual->redMask     = pScrn->mask.red;
                visual->greenMask   = pScrn->mask.green;
                visual->blueMask    = pScrn->mask.blue;
            }
        }
    }

    if (!fbPictureInit(pScreen, NULL, 0)) {
        return FALSE;
    }

    xf86SetBlackWhitePixels(pScreen);

    /* Software cursor - no hardware cursor on GOP. */
    miDCInitialize(pScreen, xf86GetPointerScreenFuncs());

    if (!miCreateDefColormap(pScreen)) {
        return FALSE;
    }

    xf86SetBackingStore(pScreen);

    /* Wrap CloseScreen so we tear down the PDGOP mapping. */
    p->CloseScreen = pScreen->CloseScreen;
    pScreen->CloseScreen = PDGOPCloseScreen;

    return TRUE;
}

static Bool
PDGOPCloseScreen(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    PDGOPPtr    p = PDGOPGetRec(pScrn);

    if (p->fbOpen) {
        PDGOPClose(&p->fb);
        p->fbOpen = FALSE;
    }
    pScreen->CloseScreen = p->CloseScreen;
    return (*pScreen->CloseScreen)(pScreen);
}

static Bool
PDGOPEnterVT(ScrnInfoPtr pScrn)
{
    (void)pScrn;
    return TRUE;
}

static void
PDGOPLeaveVT(ScrnInfoPtr pScrn)
{
    (void)pScrn;
}

static Bool
PDGOPSwitchMode(ScrnInfoPtr pScrn, DisplayModePtr mode)
{
    (void)pScrn;
    (void)mode;
    /* Only one (native) mode; nothing to switch. */
    return TRUE;
}

static void
PDGOPAdjustFrame(ScrnInfoPtr pScrn, int x, int y)
{
    (void)pScrn;
    (void)x;
    (void)y;
    /* No panning. */
}

static ModeStatus
PDGOPValidMode(ScrnInfoPtr pScrn, DisplayModePtr mode, Bool verbose, int flags)
{
    (void)pScrn;
    (void)mode;
    (void)verbose;
    (void)flags;
    return MODE_OK;
}
