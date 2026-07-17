/*
 * PureDarwin: upstream's own CMakeLists.txt already excludes CFMachPort.c/
 * CFMachPort_Lifetime.c/CFMessagePort.c from every platform's build
 * ("TODO(compnerd) make this empty on non-Mach targets" - never finished),
 * but CFRuntime.c's central class table unconditionally references
 * __CFMachPortClass and __CFMessagePortClass regardless of whether those
 * files are compiled - a data-symbol reference needing load-time binding.
 * Provide minimal, functionless class registrations so the table resolves;
 * nothing in this build actually constructs a CFMachPort/CFMessagePort
 * object (fastfetch etc. never call CFMachPortCreate/CFMessagePortCreate).
 */

#include "CFRuntime.h"
#include <mach/mach.h>
#include <mach/message.h>

/*
 * CFRunLoop.c/CFMessagePort.c call this directly; it's self-contained mach
 * message cleanup with no dependency on the excluded CFMachPort object
 * machinery above, so it's copied verbatim from CFMachPort.c rather than
 * pulling that whole (excluded) file back in.
 */
void __CFMachMessageCheckForAndDestroyUnsentMessage(kern_return_t const kr, mach_msg_header_t *const msg) {
    switch (kr) {
        case MACH_SEND_TIMEOUT:
        case MACH_SEND_INTERRUPTED: {
            mach_port_t const localPort = msg->msgh_local_port;
            if (MACH_PORT_VALID(localPort)) {
                mach_msg_bits_t const mbits = MACH_MSGH_BITS_LOCAL(msg->msgh_bits);
                if (mbits == MACH_MSG_TYPE_MOVE_SEND || mbits == MACH_MSG_TYPE_MOVE_SEND_ONCE) {
                    mach_port_deallocate(mach_task_self(), localPort);
                }
                msg->msgh_bits &= ~MACH_MSGH_BITS_LOCAL_MASK;
            }
        } // fallthrough
        case MACH_SEND_INVALID_HEADER:
        case MACH_SEND_INVALID_DEST:
        case MACH_SEND_INVALID_REPLY:
        case MACH_SEND_INVALID_VOUCHER:
            mach_msg_destroy(msg);
            break;
    }
}

const CFRuntimeClass __CFMachPortClass = {
    0,
    "CFMachPort",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};

const CFRuntimeClass __CFMessagePortClass = {
    0,
    "CFMessagePort",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};
