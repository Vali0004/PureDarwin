set(DYLD_SRC_DIR ${CMAKE_SOURCE_DIR}/src/Libraries/dyld/upstream)

add_darwin_shared_library(libdyld
    MACOSX_VERSION_MIN 11.0
    INSTALL_NAME /usr/lib/system/libdyld.dylib)
set_property(TARGET libdyld PROPERTY OUTPUT_NAME "dyld")

target_sources(libdyld PRIVATE
    ${DYLD_SRC_DIR}/src/dyldLibSystemGlue.c   # __DATA,__dyld section + _dyld_func_lookup
    ${DYLD_SRC_DIR}/src/start_glue.s          # _start (LC_MAIN startGlueToCallExit)
    ${DYLD_SRC_DIR}/src/dyld_stub_binder.s    # dyld_stub_binder for classic lazy binds
    ${DYLD_SRC_DIR}/src/threadLocalHelpers.s  # _tlv_get_addr (real asm TLV-descriptor thunk, was just unwired)
    # _tlv_atexit/_tlv_exit: real Darwin provides thread_local (C++11) teardown
    # from libdyld.dylib -- libc's atexit.c (statically bundled below) calls it
    # by that exact contract (see its own comment: "existing _tlv_atexit() in
    # libdyld"). The main `dyld` executable target also builds this file for
    # its OWN internal bootstrap use; this is a second, independent compilation
    # for the separate libdyld.dylib image, not a duplicate/conflicting symbol.
    ${DYLD_SRC_DIR}/src/threadLocalVariables.c
    # dladdr: atexit.c (bundled below) uses it to tag which image registered a
    # handler. Real, not-stubbed implementation via dyld's public image-
    # enumeration API (see the file's own header comment).
    pd_dladdr.c
    pd_libdyld_exports.c
    # Block_copy/Block_release: atexit_b()'s block-based atexit variant (same
    # translation unit as atexit()) needs the ObjC-block runtime support funcs.
    ${CMAKE_SOURCE_DIR}/src/Libraries/libSystem/libdispatch/src/BlocksRuntime/data.c
    ${CMAKE_SOURCE_DIR}/src/Libraries/libSystem/libdispatch/src/BlocksRuntime/runtime.c)

set_source_files_properties(${DYLD_SRC_DIR}/src/start_glue.s PROPERTIES
    LANGUAGE ASM COMPILE_FLAGS "-x assembler-with-cpp")
set_source_files_properties(${DYLD_SRC_DIR}/src/dyld_stub_binder.s PROPERTIES
    LANGUAGE ASM COMPILE_FLAGS "-x assembler-with-cpp")

set(_pd_cpu_caps_redirect ${CMAKE_CURRENT_BINARY_DIR}/compat-include/System/machine/cpu_capabilities.h)
set(_pd_cpu_caps_real ${CMAKE_SOURCE_DIR}/src/Kernel/xnu/osfmk/i386/cpu_capabilities.h)
file(WRITE ${_pd_cpu_caps_redirect} "#include \"${_pd_cpu_caps_real}\"\n")

target_include_directories(libdyld PRIVATE
    ${DYLD_SRC_DIR}
    ${DYLD_SRC_DIR}/include
    ${DYLD_SRC_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/compat-include)

# Like dyld itself: compile against the SDK sysroot (these TUs pull mach/*,
# pthread.h, malloc/malloc.h) instead of PD's -nostdinc header set. Drop the
# -nostdinc that add_darwin_shared_library injects; PD's own -I dirs still win.
get_target_property(_libdyld_copts libdyld COMPILE_OPTIONS)
if(_libdyld_copts)
    list(REMOVE_ITEM _libdyld_copts -nostdinc)
    set_target_properties(libdyld PROPERTIES COMPILE_OPTIONS "${_libdyld_copts}")
endif()
target_compile_options(libdyld PRIVATE
    -mno-red-zone -fno-stack-protector
    $<$<COMPILE_LANGUAGE:C>:-isysroot ${PUREDARWIN_MACOSX_SDK}>
    # bridgeos availability shim: threadLocalVariables.c pulls mach-o/dyld.h,
    # whose bridgeos __API_UNAVAILABLE entries neither the SDK nor PD define
    # (same fix dyld's own CMakeLists applies via dyld_sdk_compat.h).
    $<$<COMPILE_LANGUAGE:C>:-include${CMAKE_SOURCE_DIR}/src/Libraries/dyld/dyld_sdk_compat.h>)

# Code archives that satisfy the helper table's references (malloc/free/pthread_*/
# mmap/vm_allocate/__cxa_*) plus the raw syscall traps. Linked by path so their
# INTERFACE include dirs don't leak onto the compile line (same rationale as dyld).
add_dependencies(libdyld libsystem_kernel_static libc_static libc_internal_static
    pthread_static libplatform_static libmalloc_static libcxxabi_static_build)
target_link_libraries(libdyld PRIVATE
    $<TARGET_FILE:libsystem_kernel_static>
    $<TARGET_FILE:libplatform_static>
    $<TARGET_FILE:pthread_static>
    $<TARGET_FILE:libmalloc_static>    # malloc/free/malloc_size (helper table + atexit)
    $<TARGET_FILE:libc_static>
    $<TARGET_FILE:libc_internal_static>
    ${CMAKE_BINARY_DIR}/src/Libraries/libSystem/libsystem_kernel/syscalls.a
    $<TARGET_FILE:libcxxabi_static>)   # __cxa_atexit/__cxa_finalize[_ranges]

target_link_options(libdyld PRIVATE
    -isysroot ${PUREDARWIN_MACOSX_SDK}
    -compatibility_version 1.0.0
    -current_version 1292.100.5
    -Wl,-dead_strip
    -lSystem
    -Wl,-fixup_chains)

install(TARGETS libdyld LIBRARY DESTINATION usr/lib/system COMPONENT BaseSystem)
