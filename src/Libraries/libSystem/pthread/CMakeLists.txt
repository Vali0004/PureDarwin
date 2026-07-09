add_library(pthread_common_headers INTERFACE)
target_include_directories(pthread_common_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(pthread_common_private_headers INTERFACE)
target_include_directories(pthread_common_private_headers INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/private)

# Minimal pthread built with -DVARIANT_DYLD: the reduced locking/TSD subset a
# -nostdlib client (dyld) static-links. Under VARIANT_DYLD most of the runtime
# is elided (setspecific -> ENOTSUP, cancellation no-ops, no key destructors).
add_darwin_static_library(pthread_static)
# WIP: blocked on the shared kernel-stdatomic shadow (os/atomic_private ->
# os/atomic.h -> kernel <stdatomic.h>, no memory_order in userspace TU). Keep out
# of the default build until that's resolved; build explicitly with `ninja pthread_static`.
set_target_properties(pthread_static PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(pthread_static PRIVATE
    src/pthread.c
    src/pthread_mutex.c
    src/pthread_cond.c          # pthread_cond_wait/broadcast/signal
    src/pthread_rwlock.c        # pthread_rwlock_{rd,wr}lock/unlock
    src/pthread_tsd.c
    src/pthread_cancelable.c    # __pthread_exit_if_canceled, cancellation points
    src/pthread_atfork.c
    src/pthread_dependency.c
    src/pthread_asm.s
    pd_dyld_variant_extras.c    # _pthread_atomic_xchg_ptr + _pthread_qos_class_encode_workqueue
)
# __POSIX_LIB__: build the pthread library proper, so struct sched_param comes
# from private/pthread/posix_sched.h and pthread/sched.h only forward-declares it
# (they're mutually exclusive on this macro; without it both define -> redefinition).
# __PTHREAD_EXPOSE_INTERNALS__ + PRIVATE unlock the internal inline helpers in
# <pthread/priority_private.h> (_pthread_priority_thread_qos, _pthread_unspecified_
# priority, _pthread_priority_make_from_thread_qos) — gated #if PRIVATE &&
# __PTHREAD_EXPOSE_INTERNALS__; Apple builds libpthread's own .c files with both.
target_compile_definitions(pthread_static PRIVATE
    VARIANT_DYLD=1 __POSIX_LIB__ PRIVATE=1 __PTHREAD_EXPOSE_INTERNALS__=1)
target_include_directories(pthread_static PRIVATE
    # Reconstructed Apple-internal os/alloc_once_private.h (absent from the open
    # libplatform drop). Must precede libplatform's private/ so the wrapper is found.
    ${CMAKE_CURRENT_SOURCE_DIR}/compat-include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/resolver
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pthread   # internal.h does #include "pthread.h"
    ${CMAKE_CURRENT_SOURCE_DIR}/private
    # Userspace mach/* comes from libsystem_kernel in two halves: hand-written
    # headers (mach.h, mach_init.h, mach_interface.h) in source mach/, and the
    # MIG-generated stubs (clock_priv.h, ...) in the build-tree public_include.
    # Both are required (mach_interface.h #includes the generated ones), and the
    # generated half only exists after libsystem_kernel_static builds.
    ${CMAKE_CURRENT_SOURCE_DIR}/../libsystem_kernel/private_include
    ${CMAKE_CURRENT_SOURCE_DIR}/../libsystem_kernel/mach
    ${CMAKE_BINARY_DIR}/src/Libraries/libSystem/libsystem_kernel/public_include
    # platform/string.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../libplatform/private
    ${CMAKE_CURRENT_SOURCE_DIR}/../libmalloc/private   # stack_logging.h
)
target_link_libraries(pthread_static PRIVATE
    AvailabilityHeaders
    architecture_headers
    clang_bootstrap_headers
    libc_public_headers
    libc_private_headers
    libplatform_headers
    libplatform_private_headers
    # xnu_private_headers FIRST: the public usr/include/mach/thread_policy.h strips
    # THREAD_QOS_LAST (#ifdef PRIVATE); the System.framework PrivateHeaders copy
    # keeps it, so it must win the <mach/thread_policy.h> lookup.
    xnu_private_headers
    xnu_headers
)
# bridgeos availability shim (reuse libplatform's), force-included for C only.
target_compile_options(pthread_static PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_SOURCE_DIR}/../libplatform/pd_bridgeos_compat.h>
    # dyld-832 pthread.c vs PD's slightly older internal.h prototypes produce a
    # handful of void*<->uint64/mach_vm_address_t mismatches (_pthread_struct_init
    # stack arg, __bsdthread_register offsets arg, workq fn-ptr casts). All are
    # width-preserving on LP64 — the values passed are correct, only the declared
    # C types drift — so demote the promoted -Werror=int-conversion here.
    -Wno-int-conversion
)
set_source_files_properties(src/pthread_asm.s PROPERTIES
    LANGUAGE ASM COMPILE_FLAGS "-x assembler-with-cpp")
# pthread_tsd.c: build WITHOUT VARIANT_DYLD so it provides real dynamic TLS keys
# (pthread_key_create/delete + a working pthread_setspecific with destructors).
# dyld's own thread-local-variable support (threadLocalVariables.c) allocates
# dynamic keys with destructors at dlopen time -- the VARIANT_DYLD tsd (no keys,
# setspecific->ENOTSUP) can't serve that. The full tsd is self-contained and its
# slot layout matches what the VARIANT_DYLD pthread.c uses for the direct slots.
set_source_files_properties(src/pthread_tsd.c PROPERTIES
    COMPILE_FLAGS "-UVARIANT_DYLD -DPTHREAD_KEY_LEGACY_SUPPORT=0")
# The generated mach MIG headers (public_include) must exist before we compile.
add_dependencies(pthread_static libsystem_kernel_static)
