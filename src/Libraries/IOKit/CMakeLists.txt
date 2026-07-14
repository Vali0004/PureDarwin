# Minimal userspace IOKit client library.
#
# Nothing like this existed anywhere in the tree before - the kernel side
# (iokit_server/is_iokit_subsystem, generated from this same device.defs, and
# iokit_rpc.c's port translation helpers) is already fully built by xnu's own
# stock Makefiles; only the userspace client stubs were missing. Verified by
# hand that `mig -DIOKIT=1 -DKERNEL=0` on device.defs generates a clean
# 90-routine table (msgh_id base 2800) matching the kernel's already-compiled
# dispatch exactly, so trust the generated code here rather than hand-rolling
# wire-protocol marshaling.
#
# Just the basics for now: enough to look up a service by class name, open a
# user client connection, and map its VRAM - what a display DDX needs to
# reach IOGOPFramebuffer. Not a full IOKitLib port.

add_darwin_static_library(IOKit)

mig(${CMAKE_SOURCE_DIR}/src/Kernel/xnu/osfmk/device/device.defs
    INCLUDES_FROM_TARGET IOKit
    USER_HEADER ${CMAKE_CURRENT_BINARY_DIR}/device_user.h
    USER_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/device_user.c
    NOVOUCHERS)

target_sources(IOKit PRIVATE
    IOKitLib.c
    ${CMAKE_CURRENT_BINARY_DIR}/device_user.c
)

target_compile_definitions(IOKit PRIVATE IOKIT=1 KERNEL=0)

target_include_directories(IOKit
    PUBLIC include
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}
        # Real (non-symlinked) mach/mach.h, mach_init.h etc; libsystem_kernel
        # keeps this PRIVATE to itself, and its own include/mach/*.h files are
        # dangling symlinks pointing at the wrong relative path, so reach the
        # real headers directly instead.
        ${CMAKE_SOURCE_DIR}/src/Libraries/libSystem/libsystem_kernel/mach
)

target_link_libraries(IOKit PUBLIC
    xnu_headers
    xnu_private_headers
    libc_public_headers
    # host_get_io_master/NDR_record/mach_host_self and the generated
    # mach/mach_host.h (host_get_io_master's prototype) live here.
    libsystem_kernel_static
    clang_bootstrap_headers
)

install(TARGETS IOKit DESTINATION usr/local/lib COMPONENT DeveloperInternal)
install(FILES include/PDIOKitLib.h DESTINATION usr/local/include COMPONENT DeveloperInternal)
install(FILES include/IOKit/IOKitLib.h DESTINATION usr/local/include/IOKit COMPONENT DeveloperInternal)
