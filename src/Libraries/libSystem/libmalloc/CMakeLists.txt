# Currently, just expose headers under include/
add_library(libmalloc_headers INTERFACE)
target_include_directories(libmalloc_headers INTERFACE include)

add_library(libmalloc_private_headers INTERFACE)
target_include_directories(libmalloc_private_headers INTERFACE private)

# libmalloc_static: the real magazine/nano allocator, built as a static archive
# to be linked into (and exported by) libSystem.B.dylib. Compiled against the SDK
# sysroot like the other libsystem_* static pieces (its .c files pull mach/*,
# os/*, mach-o/dyld_priv.h). EXCLUDE_FROM_ALL -- built on demand for libSystem.
add_library(libmalloc_static STATIC EXCLUDE_FROM_ALL
    src/malloc.c
    src/malloc_common.c
    src/malloc_printf.c
    src/magazine_malloc.c
    src/magazine_large.c
    src/magazine_medium.c
    src/magazine_small.c
    src/magazine_tiny.c
    src/magazine_rack.c
    src/nano_malloc.c
    src/nano_malloc_common.c
    src/nanov2_malloc.c
    src/pd_nanov2_fallback.c   # nanov2_{create_zone,init,configure,forked_zone}: see file comment
    src/purgeable_malloc.c
    src/pguard_malloc.c
    src/legacy_malloc.c
    src/frozen_malloc.c
    src/bitarray.c
    src/vm.c
    src/msl_lite_support.c   # set_msl_lite_hooks (real implementation, just wasn't wired in)
)

set_property(TARGET libmalloc_static PROPERTY OSX_ARCHITECTURES "${PUREDARWIN_ARCH}")

target_compile_definitions(libmalloc_static PRIVATE
    MALLOC_TARGET_EXCLAVES=0)

target_include_directories(libmalloc_static PRIVATE
    # Vendored Apple-internal headers absent from the SDK (os/feature_private.h,
    # ...), so the build doesn't depend on sibling checkouts. Must precede the
    # SDK/xnu dirs so these win the <os/*> lookup.
    ${CMAKE_CURRENT_SOURCE_DIR}/compat-include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/private
    ${CMAKE_CURRENT_SOURCE_DIR}/resolver
    # mach-o/dyld_priv.h (not in the SDK) from the vendored dyld tree.
    ${CMAKE_SOURCE_DIR}/src/Libraries/dyld/upstream/include
    # pthread/private.h, tsd_private.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../pthread/private
    ${CMAKE_CURRENT_SOURCE_DIR}/../pthread/include
    # libc thread_stack_pcs.h (NB: do NOT add libc/include -- its sys/cdefs.h
    # shadows the SDK's and wants generated libc-features.h).
    ${CMAKE_CURRENT_SOURCE_DIR}/../libc/gen
    ${CMAKE_CURRENT_SOURCE_DIR}/../libc/private
    # userspace mach/* (hand-written + MIG-generated halves), same as pthread.
    ${CMAKE_CURRENT_SOURCE_DIR}/../libsystem_kernel/private_include
    ${CMAKE_CURRENT_SOURCE_DIR}/../libsystem_kernel/mach
    ${CMAKE_BINARY_DIR}/src/Libraries/libSystem/libsystem_kernel/public_include
    # libplatform's <libkern/OSAtomic{,Deprecated,Queue}.h> (real userspace
    # OSAtomicIncrement32Barrier/OSQueueHead/... declarations) MUST precede
    # xnu/libkern below: xnu ships ITS OWN <libkern/OSAtomic.h> (kernel-side,
    # different guard _OS_OSATOMIC_H, no userspace Barrier API) at the same
    # relative path, which would otherwise shadow it via -I search order.
    ${CMAKE_CURRENT_SOURCE_DIR}/../libplatform/private
    ${CMAKE_CURRENT_SOURCE_DIR}/../libplatform/include
    # os/atomic_private.h (+ _impl/_arch siblings) -- only source we have for
    # this one; harmless now that libplatform's libkern/OSAtomic.h already won.
    ${CMAKE_SOURCE_DIR}/src/Kernel/xnu/libkern)

target_link_libraries(libmalloc_static PRIVATE
    AvailabilityHeaders
    architecture_headers
    clang_bootstrap_headers
    libc_public_headers
    libc_private_headers
    libplatform_headers
    libplatform_private_headers
    xnu_private_headers
    xnu_headers)

target_compile_options(libmalloc_static PRIVATE
    -mno-red-zone -fno-stack-protector
    -isysroot ${PUREDARWIN_MACOSX_SDK}
    -Wno-availability
    # bridgeos availability shim + PD SDK compat (reuse the existing ones).
    $<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_SOURCE_DIR}/../libplatform/pd_bridgeos_compat.h>
    $<$<COMPILE_LANGUAGE:C>:-include${CMAKE_SOURCE_DIR}/src/Libraries/dyld/dyld_sdk_compat.h>)

# The MIG-generated mach headers (public_include) must exist first.
add_dependencies(libmalloc_static libsystem_kernel_static)
