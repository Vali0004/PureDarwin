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
