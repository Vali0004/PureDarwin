#include <IOKit/IOLib.h>
#include <kern/thread.h>
#include <kern/thread_call.h>
#include <miscfs/devfs/devfs.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include "RavynXHCIMouse.h"
#include "RavynXHCIPort.h"

#define super IOService
OSDefineMetaClassAndStructors(RavynXHCIMouse, IOService);

extern "C" int devfs_is_ready(void);

struct XHCIMouseEvent {
    UInt32 sequence;
    UInt8 mouseIndex;
    UInt8 buttons;
    SInt8 dx;
    SInt8 dy;
    SInt8 wheel;
    UInt8 reserved[3];
};

static void *gMouseNode;
static int gMouseMajor = -1;
static thread_call_t gMouseRetryCall;
static IONotifier *gMouseBSDNotifier;
static IOLock *gMouseLock;
static UInt32 gMouseSequence;
static UInt32 gMouseReadIndex;
static UInt32 gMouseWriteIndex;
static XHCIMouseEvent gMouseEvents[64];

static void xhci_mouse_publish_retry(thread_call_param_t, thread_call_param_t);
static bool xhci_mouse_iobsd_published(void *, void *, IOService *, IONotifier *);

static int
xhci_mouse_open(dev_t, int, int, struct proc *)
{
    return 0;
}

static int
xhci_mouse_close(dev_t, int, int, struct proc *)
{
    return 0;
}

static int
xhci_mouse_read(dev_t, struct uio *uio, int)
{
    XHCIMouseEvent event;
    int error = 0;

    if (!gMouseLock) {
        return ENXIO;
    }
    if (uio_resid(uio) < (user_ssize_t)sizeof(event)) {
        return EINVAL;
    }

    IOLockLock(gMouseLock);
    if (gMouseReadIndex == gMouseWriteIndex) {
        error = EAGAIN;
    } else {
        event = gMouseEvents[gMouseReadIndex % (UInt32)(sizeof(gMouseEvents) / sizeof(gMouseEvents[0]))];
        gMouseReadIndex++;
    }
    IOLockUnlock(gMouseLock);

    if (error) {
        return error;
    }
    return uiomove((const char *)&event, (int)sizeof(event), uio);
}

static struct cdevsw xhci_mouse_cdevsw =
{
    /* d_open     */ xhci_mouse_open,
    /* d_close    */ xhci_mouse_close,
    /* d_read     */ xhci_mouse_read,
    /* d_write    */ eno_rdwrt,
    /* d_ioctl    */ eno_ioctl,
    /* d_stop     */ eno_stop,
    /* d_reset    */ eno_reset,
    /* d_ttys     */ 0,
    /* d_select   */ eno_select,
    /* d_mmap     */ eno_mmap,
    /* d_strategy */ eno_strat,
    /* d_getc     */ eno_getc,
    /* d_putc     */ eno_putc,
    /* d_type     */ 0
};

static void
xhci_mouse_schedule_retry()
{
    AbsoluteTime deadline;

    if (gMouseNode) {
        return;
    }
    if (!gMouseRetryCall) {
        gMouseRetryCall = thread_call_allocate(xhci_mouse_publish_retry, NULL);
        if (!gMouseRetryCall) {
            return;
        }
    }

    clock_interval_to_deadline(1, kSecondScale, &deadline);
    thread_call_enter_delayed(gMouseRetryCall, deadline);
}

static void
xhci_mouse_publish()
{
    if (gMouseNode) {
        return;
    }
    if (!gMouseLock) {
        gMouseLock = IOLockAlloc();
        if (!gMouseLock) {
            return;
        }
    }
    if (gMouseMajor < 0) {
        gMouseMajor = cdevsw_add(-1, &xhci_mouse_cdevsw);
        if (gMouseMajor < 0) {
            return;
        }
    }

    if (!devfs_is_ready()) {
        if (!gMouseBSDNotifier) {
            OSDictionary *matching = IOService::resourceMatching("IOBSD");
            if (matching) {
                gMouseBSDNotifier = IOService::addMatchingNotification(gIOPublishNotification,
                                                                       matching,
                                                                       xhci_mouse_iobsd_published,
                                                                       NULL,
                                                                       NULL);
                matching->release();
            }
        }
        xhci_mouse_schedule_retry();
        return;
    }

    gMouseNode = devfs_make_node(makedev(gMouseMajor, 0), DEVFS_CHAR,
                                 0, 0, 0666, "xhci_mouse");
    if (!gMouseNode) {
        xhci_mouse_schedule_retry();
    } else {
        IOLog("RavynXHCIMouse: published /dev/xhci_mouse\n");
    }
}

static void
xhci_mouse_publish_retry(thread_call_param_t, thread_call_param_t)
{
    xhci_mouse_publish();
}

static bool
xhci_mouse_iobsd_published(void *, void *, IOService *, IONotifier *notifier)
{
    xhci_mouse_publish();

    if (gMouseNode && notifier) {
        notifier->remove();
        if (gMouseBSDNotifier == notifier) {
            gMouseBSDNotifier = NULL;
        }
    }
    return true;
}

static void
xhci_mouse_push_event(UInt8 mouseIndex, UInt8 buttons, SInt8 dx, SInt8 dy, SInt8 wheel)
{
    UInt32 slot;
    XHCIMouseEvent event;

    if (!gMouseLock) {
        xhci_mouse_publish();
        if (!gMouseLock) {
            return;
        }
    }

    event.sequence = ++gMouseSequence;
    event.mouseIndex = mouseIndex;
    event.buttons = buttons;
    event.dx = dx;
    event.dy = dy;
    event.wheel = wheel;
    bzero(event.reserved, sizeof(event.reserved));

    IOLockLock(gMouseLock);
    slot = gMouseWriteIndex % (UInt32)(sizeof(gMouseEvents) / sizeof(gMouseEvents[0]));
    gMouseEvents[slot] = event;
    gMouseWriteIndex++;
    if (gMouseWriteIndex - gMouseReadIndex >
        (UInt32)(sizeof(gMouseEvents) / sizeof(gMouseEvents[0]))) {
        gMouseReadIndex = gMouseWriteIndex -
            (UInt32)(sizeof(gMouseEvents) / sizeof(gMouseEvents[0]));
    }
    IOLockUnlock(gMouseLock);
}

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

    xhci_mouse_publish();

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
        xhci_mouse_push_event((UInt8)_mouseIndex, buttons, dx, dy, wheel);
        _lastButtons = buttons;
    }
}
