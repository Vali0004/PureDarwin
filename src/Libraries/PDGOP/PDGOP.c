#include <PDGOP.h>
#include <IOKit/IOKitLib.h>
#include <mach/mach.h>
#include <string.h>

#define kIOFBVRAMMemory 110
#define kIOFBGetPixelInformationSelector 1
#define kIOFBGetCurrentDisplayModeSelector 2
#define kIOFBSystemAperture 0

typedef uint32_t IODisplayModeID;
typedef uint32_t IOIndex;
typedef uint32_t IOPixelAperture;
typedef char IOPixelEncoding[64];

typedef struct IOPixelInformation {
    uint32_t bytesPerRow;
    uint32_t bytesPerPlane;
    uint32_t bitsPerPixel;
    uint32_t pixelType;
    uint32_t componentCount;
    uint32_t bitsPerComponent;
    uint32_t componentMasks[16];
    IOPixelEncoding pixelFormat;
    uint32_t flags;
    uint32_t activeWidth;
    uint32_t activeHeight;
    uint32_t reserved[2];
} IOPixelInformation;

kern_return_t
PDGOPOpen(PDGOPFramebuffer *fb)
{
    kern_return_t kr;
    char *matching;
    uint64_t scalarOut[2] = {0, 0};
    uint32_t scalarOutCnt = 2;
    uint64_t scalarIn[3];
    IOPixelInformation pixelInfo;
    size_t pixelInfoSize = sizeof(pixelInfo);

    if (fb == NULL) {
        return KERN_INVALID_ARGUMENT;
    }
    memset(fb, 0, sizeof(*fb));

    kr = IOMasterPort(MACH_PORT_NULL, &fb->masterPort);
    if (kr != KERN_SUCCESS) {
        goto fail;
    }

    matching = IOServiceMatching("IOGOPFramebuffer");
    if (matching == NULL) {
        kr = KERN_RESOURCE_SHORTAGE;
        goto fail;
    }

    kr = IOServiceGetMatchingService(fb->masterPort, matching, &fb->service);
    if (kr != KERN_SUCCESS || fb->service == IO_OBJECT_NULL) {
        if (kr == KERN_SUCCESS) {
            kr = KERN_FAILURE;
        }
        goto fail;
    }

    kr = IOServiceOpen(fb->service, mach_task_self(), 0, &fb->connect);
    if (kr != KERN_SUCCESS) {
        goto fail;
    }

    kr = IOConnectCallScalarMethod(fb->connect,
        kIOFBGetCurrentDisplayModeSelector, NULL, 0, scalarOut, &scalarOutCnt);
    if (kr != KERN_SUCCESS || scalarOutCnt < 2) {
        goto fail;
    }

    scalarIn[0] = (IODisplayModeID)scalarOut[0];
    scalarIn[1] = (IOIndex)scalarOut[1];
    scalarIn[2] = kIOFBSystemAperture;
    memset(&pixelInfo, 0, sizeof(pixelInfo));
    kr = IOConnectCallMethod(fb->connect,
        kIOFBGetPixelInformationSelector, scalarIn, 3, NULL, 0,
        NULL, NULL, &pixelInfo, &pixelInfoSize);
    if (kr != KERN_SUCCESS) {
        goto fail;
    }

    kr = IOConnectMapMemory64(fb->connect, kIOFBVRAMMemory, mach_task_self(),
        &fb->address, &fb->size, kIOMapAnywhere);
    if (kr != KERN_SUCCESS) {
        goto fail;
    }

    fb->width = pixelInfo.activeWidth;
    fb->height = pixelInfo.activeHeight;
    fb->stride = pixelInfo.bytesPerRow;
    fb->bpp = pixelInfo.bitsPerPixel;
    fb->pixelType = pixelInfo.pixelType;
    fb->componentMasks[0] = pixelInfo.componentMasks[0];
    fb->componentMasks[1] = pixelInfo.componentMasks[1];
    fb->componentMasks[2] = pixelInfo.componentMasks[2];

    return KERN_SUCCESS;

fail:
    PDGOPClose(fb);
    return kr;
}

void
PDGOPClose(PDGOPFramebuffer *fb)
{
    if (fb == NULL) {
        return;
    }
    if (fb->address != 0 && fb->connect != IO_CONNECT_NULL) {
        (void)IOConnectUnmapMemory64(fb->connect, kIOFBVRAMMemory,
            mach_task_self(), fb->address);
    }
    if (fb->connect != IO_CONNECT_NULL) {
        (void)IOServiceClose(fb->connect);
        (void)IOObjectRelease(fb->connect);
    }
    if (fb->service != IO_SERVICE_NULL) {
        (void)IOObjectRelease(fb->service);
    }
    if (fb->masterPort != MACH_PORT_NULL) {
        (void)mach_port_deallocate(mach_task_self(), fb->masterPort);
    }
    memset(fb, 0, sizeof(*fb));
}
