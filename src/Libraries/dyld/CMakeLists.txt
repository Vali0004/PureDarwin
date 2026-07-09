# Builds the runtime dynamic linker /usr/lib/dyld (dyld-832, dyld2 path).
#
# The dyld source lives in a sibling checkout (github.com/.../dyld). Point
# DYLD_SOURCE_DIR at it; defaults to ../dyld next to the PureDarwin checkout.
set(DYLD_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../dyld" CACHE PATH "Path to the dyld source checkout")

if(NOT EXISTS "${DYLD_SOURCE_DIR}/src/dyld2.cpp")
    message(WARNING "dyld source not found at ${DYLD_SOURCE_DIR}; skipping dyld target")
    return()
endif()

# Runtime linker sources: the src/ dyld2 family plus the non-cache-builder
# dyld3 support code it depends on. The shared-cache *builder* sources
# (CacheBuilder, Optimizer*, AppCache*, update_*, dsc_*) are deliberately
# excluded -- they belong to the cache tooling, not the runtime linker.
set(DYLD_SRCS
    src/dyld2.cpp
    src/dyldAPIs.cpp
    src/dyldInitialization.cpp
    src/dyldNew.cpp
    src/dyldExceptions.c
    src/dyldLock.cpp
    # dyld_debugger.cpp: the all_image_infos / GDB-notification management
    # (addImagesToAllImages, allImagesCount, gProcessInfo, notifyGDB,
    # removeImageFromAllImages, addNonSharedCacheImageUUID). No dispatch/ObjC deps.
    src/dyld_debugger.cpp
    src/glue.c
    src/threadLocalVariables.c
    src/ImageLoader.cpp
    src/ImageLoaderMachO.cpp
    src/ImageLoaderMachOClassic.cpp
    src/ImageLoaderMachOCompressed.cpp
    src/ImageLoaderMegaDylib.cpp

    dyld3/AllImages.cpp
    dyld3/APIs.cpp
    dyld3/APIs_macOS.cpp
    dyld3/BootArgs.cpp
    dyld3/Closure.cpp
    dyld3/ClosureBuilder.cpp
    dyld3/ClosureFileSystemNull.cpp
    dyld3/ClosureFileSystemPhysical.cpp
    dyld3/ClosureWriter.cpp
    dyld3/Diagnostics.cpp
    # JSONReader.mm deliberately omitted: it needs Foundation/ObjC and is only used
    # by the closure-builder tooling, never the dyld2 runtime linker path. dyld
    # loads Foundation, so it cannot depend on it.
    dyld3/Loading.cpp
    dyld3/Logging.cpp
    dyld3/MachOAnalyzer.cpp
    dyld3/MachOAnalyzerSet.cpp
    dyld3/MachOFile.cpp
    dyld3/MachOLoaded.cpp
    dyld3/PathOverrides.cpp
    dyld3/RootsChecker.cpp
    dyld3/SharedCacheRuntime.cpp
    dyld3/Tracing.cpp
    dyld3/libdyldEntryVector.cpp
    dyld3/shared-cache/DyldSharedCache.cpp

    src/dyldStartup.s
    src/dyld_stub_binder.s
    src/threadLocalHelpers.s
    src/stub_binding_helper.s
    src/start_glue.s
)
list(TRANSFORM DYLD_SRCS PREPEND "${DYLD_SOURCE_DIR}/")

add_darwin_executable(dyld MACOSX_VERSION_MIN 11.0)
# WIP: dyld does not link yet (still ~113 undefined: C++ runtime, libc, pthread).
# Keep it out of the default build so `ninja all` stays green; build it explicitly
# with `ninja dyld` when working the link down.
set_target_properties(dyld PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(dyld PRIVATE ${DYLD_SRCS}
    ${CMAKE_CURRENT_SOURCE_DIR}/pd_stubs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/pd_fileutils.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/pd_dyld_internal_stubs.cpp)

# add_darwin_executable forces -nostdinc to build against PD's own headers, but
# dyld pulls libc++ STL (vector/atomic/...) whose wrapper headers #include_next
# into the C library; under -nostdinc that chain is broken and unrecoverable.
# dyld links nothing (self-contained), so compiling against the SDK's coherent
# libc/libc++ is correct -- and is how Apple builds dyld. PD's headers still take
# priority via -I. Keep -nostdlib (no default libs linked); drop only -nostdinc.
get_target_property(_dyld_copts dyld COMPILE_OPTIONS)
list(REMOVE_ITEM _dyld_copts -nostdinc)
set_target_properties(dyld PROPERTIES COMPILE_OPTIONS "${_dyld_copts}")

set_source_files_properties(
    "${DYLD_SOURCE_DIR}/src/dyldStartup.s"
    "${DYLD_SOURCE_DIR}/src/dyld_stub_binder.s"
    "${DYLD_SOURCE_DIR}/src/threadLocalHelpers.s"
    "${DYLD_SOURCE_DIR}/src/stub_binding_helper.s"
    "${DYLD_SOURCE_DIR}/src/start_glue.s"
    PROPERTIES LANGUAGE ASM COMPILE_FLAGS "-x assembler-with-cpp"
)

target_include_directories(dyld PRIVATE
    ${DYLD_SOURCE_DIR}
    ${DYLD_SOURCE_DIR}/include
    ${DYLD_SOURCE_DIR}/src
    ${DYLD_SOURCE_DIR}/dyld3
    ${DYLD_SOURCE_DIR}/dyld3/shared-cache
    ${CMAKE_SOURCE_DIR}/src/Libraries/libSystem/corecrypto/include
    ${CMAKE_SOURCE_DIR}/src/Libraries/CrashReporterClient/include
    # NB: do NOT add libsystem_kernel/mach or /include here -- their root holds a
    # minimal internal string.h (memcpy-only) that shadows the C library's, and
    # their mach/* entries are broken symlinks. The SDK sysroot (-isysroot) already
    # provides a complete, coherent mach/* header set.
    # Headers no source interface target ships: userspace mach-o/* and top-level
    # pthread.h. Committed under compat-include/ so the build is reproducible.
    ${CMAKE_CURRENT_SOURCE_DIR}/compat-include
)

# _DARWIN_C_SOURCE exposes BSD extensions (strnlen etc.) that XNU's kcdata.h,
# leaked into dyld2.cpp's include chain, relies on.
# BUILDING_LIBDYLD too: the dyld3 runtime files (AllImages/APIs/...) are the
# "libdyld" launch-closure code compiled into dyld, and gate types like
# GrowableArray on BUILDING_LIBDYLD.
# CORECRYPTO_DONOT_USE_TRANSPARENT_UNION: transparent_union is a C-only attribute
# (ignored in C++), so corecrypto's ccdigest_ctx_t union won't accept a plain
# ccdigest_ctx pointer in dyld's C++ TUs; force the plain-pointer typedef.
# _FORTIFY_SOURCE=0: avoids the __*_chk fortified libc variants (___memcpy_chk,
# ___memset_chk) which would otherwise need a libc we don't link.
target_compile_definitions(dyld PRIVATE NDEBUG BUILDING_DYLD=1 BUILDING_LIBDYLD=1
    _DARWIN_C_SOURCE CORECRYPTO_DONOT_USE_TRANSPARENT_UNION _FORTIFY_SOURCE=0)

# Ensure the MIG-generated mach public headers exist before dyld compiles.
add_dependencies(dyld libsystem_kernel_static)

# dyld uses C++ exceptions (throw/try) for its error path. It must be built with
# -mno-red-zone like the rest of the low-level Darwin code, and needs the
# bridgeos/TARGET_OS_BRIDGE availability shim force-included (see the header).
target_compile_options(dyld PRIVATE
    -mno-red-zone -fno-stack-protector
    # PD normally builds -nostdinc with explicit -I header dirs, so it sets no
    # sysroot. dyld compiles against the SDK's coherent libc/libc++ instead (its
    # wrapper headers #include_next into the C library), so it needs -isysroot.
    # PD's own headers still take priority via -I.
    -isysroot ${PUREDARWIN_MACOSX_SDK}
    # The C++ compat force-include must NOT reach the .s files (it would inject
    # C++ into assembly); scope it to C/C++ translation units only.
    $<$<COMPILE_LANGUAGE:C,CXX>:-include${CMAKE_CURRENT_SOURCE_DIR}/dyld_sdk_compat.h>
)
set_source_files_properties(${DYLD_SRCS} PROPERTIES
    COMPILE_OPTIONS "$<$<COMPILE_LANGUAGE:CXX>:-std=c++17;-fno-rtti>")

target_link_libraries(dyld PRIVATE
    AvailabilityHeaders
    dyld_availability_headers   # generated dyld/VersionMap.h + for_dyld_priv.inc
    clang_bootstrap_headers
    architecture_headers
    libc_public_headers
    libc_private_headers
    libc_kernel_headers
    pthread_common_headers
    pthread_common_private_headers
    libplatform_headers
    libplatform_private_headers
    xnu_headers
    xnu_private_headers
)

# Actual code archives to satisfy dyld's -nostdlib symbol needs. Link them by
# file path (not as CMake targets) so their INTERFACE include dirs -- which carry
# XNU Kernel.framework headers that shadow the SDK's mach-o/* -- do NOT leak onto
# dyld's compile line. add_dependencies keeps build ordering.
add_dependencies(dyld libsystem_kernel_static corecrypto_static CrashReporterClient libplatform_static libc_static libc_internal_static pthread_static)
target_link_libraries(dyld PRIVATE
    $<TARGET_FILE:libsystem_kernel_static>   # Mach RPC + higher-level wrappers
    # libplatform: string/mem primitives (memcpy/memset/strlen/...) + OSAtomic.
    $<TARGET_FILE:libplatform_static>
    # pthread (VARIANT_DYLD): mutex/cond/rwlock/tsd/self for dyld's own locking
    # and libc++abi's exception paths.
    $<TARGET_FILE:pthread_static>
    # libc: leaf funcs (strcat/strdup/strrchr/qsort/realpath/*_chk/subsystem).
    $<TARGET_FILE:libc_static>
    # libc internal-header half: dir I/O (opendir/readdir/closedir) built
    # -nostdinc against Libc's internal DIR. (stdio/locale/getcwd to follow.)
    $<TARGET_FILE:libc_internal_static>
    # syscalls.a: raw BSD syscall traps (_write/_socket/_syscall...). Its CMake
    # IMPORTED target (libsyscall_traps) is directory-scoped and not visible here,
    # so reference the archive by its known build path.
    ${CMAKE_BINARY_DIR}/src/Libraries/libSystem/libsystem_kernel/syscalls.a
    $<TARGET_FILE:corecrypto_static>         # _ccdigest_*/_ccsha*
    $<TARGET_FILE:CrashReporterClient>       # _gCRAnnotations
)

# libc++abi (Itanium C++ ABI runtime): __cxa_*, operator new/delete, personality,
# std::*_error + typeinfo/vtables. Built from the vendored in-tree libc++abi (see
# src/Libraries/libcxxabi). Link after the libc/platform archives so its
# malloc/pthread/abort references resolve.
if(TARGET libcxxabi_static)
    add_dependencies(dyld libcxxabi_static_build)
    target_link_libraries(dyld PRIVATE $<TARGET_FILE:libcxxabi_static>)
endif()
# libunwind: the _Unwind_* EH machinery libc++abi's personality routine drives.
if(TARGET libunwind_static)
    add_dependencies(dyld libunwind_static_build)
    target_link_libraries(dyld PRIVATE $<TARGET_FILE:libunwind_static>)
endif()

# dyld is the dynamic linker: MH_DYLINKER, custom entry point, self-relative.
#
# REVERTED an explicit -segaddr __TEXT base (tried 0x7fff5fc00000, then
# 0x200000000, to dodge a suspected page-zero collision): mach_loader.c's
# LC_SEGMENT pass explicitly checks `is_dyld && scp->vmaddr == 0 &&
# scp->fileoff == 0` to set dyld_no_load_addr -- vmaddr==0 is the shape the
# kernel's loader EXPECTS from a dylinker (matching real Apple dyld, which
# always links at 0 and is placed by the kernel's own slide computation at
# load time). Forcing a nonzero link-time base defeats that detection and
# pushes dyld onto a different, apparently untested kernel code path -- and
# rebooting with the forced base did NOT change the crash signature at all,
# ruling out the page-zero theory. Back to the default (vmaddr 0).
target_link_options(dyld PRIVATE
    -e __dyld_start
    -Wl,-dylinker
    -Wl,-dylinker_install_name,/usr/lib/dyld
    -Wl,-fixup_chains
    -Wl,-data_const
    # dyld is normally dead-stripped. It also lets the linker drop the dead code
    # inside the monolithic pthread.o (workqueue/bsdthread_init/qos, pulled in
    # only because pthread_self et al share the TU) along with its undefined refs
    # to thread_start/start_wqthread/_pthread_qos_*/_pthread_set_main_qos -- none
    # of which dyld's used call graph reaches.
    -Wl,-dead_strip
)

install(TARGETS dyld RUNTIME DESTINATION usr/lib COMPONENT BaseSystem)
