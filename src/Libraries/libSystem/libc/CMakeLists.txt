# Currently, just expose standard headers under include

# We need to generate libc-features.h for even the public headers to work
# THIS IS NOT TRUE. sys/cdefs.h in the include/ directory here IS NOT the actual public header.
# We need to exclude it from our search paths.
add_custom_command(
    OUTPUT
        ${CMAKE_CURRENT_BINARY_DIR}/include/libc-features.h
    COMMAND ${CMAKE_COMMAND} -E env "ARCHS=x86_64 arm64" SRCROOT=${CMAKE_CURRENT_SOURCE_DIR} DERIVED_FILES_DIR=${CMAKE_CURRENT_BINARY_DIR}/include VARIANT_PLATFORM_NAME=macosx perl ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_features.pl
    COMMENT "Generating libc-features.h"
)

# The perl script won't do this for us
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include)

add_darwin_object_library(libc_features)

target_include_directories(libc_features INTERFACE ${CMAKE_CURRENT_BINARY_DIR}/include)

target_sources(libc_features PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/include/libc-features.h
)

# END GENERATE libc-features.h

# We install the standard C headers to a skeleton tree here
set(libc_headers_dir ${CMAKE_CURRENT_BINARY_DIR}/headers)

add_custom_command(
    OUTPUT ${libc_headers_dir}/libc_headers.stamp
    COMMAND ${CMAKE_COMMAND} -E env "ARCHS=arm64 x86_64"
        SRCROOT=${CMAKE_CURRENT_SOURCE_DIR}
        DERIVED_FILES_DIR=${CMAKE_CURRENT_BINARY_DIR}
        VARIANT_PLATFORM_NAME=macosx
        DEPLOYMENT_LOCATION=NO
        BUILT_PRODUCTS_DIR=${libc_headers_dir}
        SDK_INSTALL_HEADERS_ROOT=""
        sh -c "${CMAKE_CURRENT_SOURCE_DIR}/scripts/headers.sh"
    COMMAND ${CMAKE_COMMAND} -E touch ${libc_headers_dir}/libc_headers.stamp
    COMMENT "Copying libC headers..."
)
add_custom_target(libc_headers_gen_script DEPENDS ${libc_headers_dir}/libc_headers.stamp)

add_library(libc_headers_gen INTERFACE)
add_library(libc_headers_gen_private INTERFACE)

target_include_directories(libc_headers_gen INTERFACE ${libc_headers_dir}/usr/include)
add_dependencies(libc_headers_gen INTERFACE libc_headers_gen_script)

target_include_directories(libc_headers_gen_private INTERFACE
    ${libc_headers_dir}/usr/include
    ${libc_headers_dir}/usr/local/include
    ${libc_headers_dir}/System/Library/Frameworks/Kernel.framework/Versions/A/PrivateHeaders
    ${libc_headers_dir}/System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders
)

# This is the only public header directory
install(DIRECTORY ${libc_headers_dir}/usr/include DESTINATION usr COMPONENT DeveloperTools)

# These are private
install(DIRECTORY ${libc_headers_dir}/usr/local/include DESTINATION usr/local COMPONENT DeveloperInternal)
install(DIRECTORY ${libc_headers_dir}/System/Library/Frameworks/Kernel.framework/Versions/A/PrivateHeaders DESTINATION System/Library/Frameworks/Kernel.framework/Versions/A/PrivateHeaders COMPONENT DeveloperInternal)
install(DIRECTORY ${libc_headers_dir}/System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders DESTINATION System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders COMPONENT DeveloperInternal)

# END GENERATE INCLUDE DIRECTORY

add_library(libc_private_headers INTERFACE)
add_library(libc_kernel_headers INTERFACE)
add_library(libc_public_headers INTERFACE)

# stdlib.h includes malloc/_malloc.h
target_link_libraries(libc_public_headers INTERFACE libmalloc_headers)

# private headers implies public headers
target_link_libraries(libc_private_headers INTERFACE libc_public_headers)

# Make sure we install headers before use
target_link_libraries(libc_public_headers INTERFACE libc_headers_gen)
target_link_libraries(libc_kernel_headers INTERFACE libc_headers_gen)

target_include_directories(libc_public_headers INTERFACE ${libc_headers_dir}/usr/include)

target_include_directories(libc_private_headers INTERFACE ${libc_headers_dir}/System/Library/Frameworks/System.framework/Versions/B/PrivateHeaders)
target_include_directories(libc_private_headers INTERFACE ${libc_headers_dir}/usr/local/include)

target_include_directories(libc_kernel_headers INTERFACE ${libc_headers_dir}/System/Library/Frameworks/Kernel.framework/Versions/A/PrivateHeaders)

# END HEADER INTERFACE

# A focused static archive of the libc routines dyld references when linked
# -nostdlib (strings, dir I/O, stdlib, fortify, subsystem SPI, C-locale rune
# table). Not the whole of libc -- just the symbols the runtime linker needs.
# WIP / opt-in (EXCLUDE_FROM_ALL): build with `ninja libc_static`.
add_darwin_static_library(libc_static)
target_sources(libc_static PRIVATE
    string/strcat.c
    string/FreeBSD/strdup.c
    string/FreeBSD/strrchr.c
    stdlib/FreeBSD/qsort.c
    stdlib/FreeBSD/realpath.c
    secure/memcpy_chk.c
    secure/memset_chk.c
    darwin/subsystem.c
    stdlib/FreeBSD/heapsort.c    # _heapsort
    stdlib/FreeBSD/getenv.c      # _getenv/_setenv/_unsetenv
    gen/FreeBSD/sysctlbyname.c   # _sysctlbyname (-> __sysctlbyname syscall)
    sys/crt_externs.c            # _NSGetEnviron/_NSGetArgv/_NSGetArgc/_NSGetProgname
    # TODO (need cross-deps beyond the SDK sysroot):
    #  - secure/chk_fail.c (___chk_fail_overflow) pulls <mach-o/dyld_priv.h> (dyld)
    #  - gen/FreeBSD/arc4random.c pulls os/assumes.h -> <os/log_private.h> (libkern)
    #  - stdio/FreeBSD/mktemp.c (_mkstemp) needs BSD-internal TRUE/FALSE/bool
    # all deferred to the full internal libc env.
)
# TODO (deferred - need the full -nostdinc PD-libc INTERNAL environment, not the
# SDK): dir functions opendir/readdir/closedir (internal un-namespaced DIR:
# dd_lock/dd_fd vs SDK __dd_*), getcwd (apfs/apfs_fsctl.h), arc4random
# (os/assumes.h), getenv (os/lock_private.h), mktemp (sys-internal TRUE), and
# locale/FreeBSD/table.c for __maskrune/__DefaultRuneLocale (deep locale chain:
# xlocale_private -> setlocale.h -> timelocal.h -> ...).
set_target_properties(libc_static PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Compile against the SDK sysroot (like dyld): the libc *.c definitions build
# against the SDK's coherent libc/pthread/mach *declarations* (same macOS 11
# vintage, matching struct layouts), which resolves the whole SDK-assumption
# header cluster (<pthread.h>, gethostuuid.h, os/lock_private.h, bridgeos
# availability) in one move. Libc-internal headers still win via -I. So strip
# add_darwin_static_library's -nostdinc and add -isysroot.
get_target_property(_libc_copts libc_static COMPILE_OPTIONS)
list(REMOVE_ITEM _libc_copts -nostdinc)
set_target_properties(libc_static PROPERTIES COMPILE_OPTIONS "${_libc_copts}")
target_compile_options(libc_static PRIVATE -isysroot ${PUREDARWIN_MACOSX_SDK})
# _FORTIFY_SOURCE=0: the *_chk source files (and secure/_string.h) otherwise
# collide with the compiler's __builtin___strcat_chk etc.
target_compile_definitions(libc_static PRIVATE _FORTIFY_SOURCE=0)
# bridgeos availability shim (reuse libplatform's), force-included for C: several
# private headers (os/lock_private.h) use __API_AVAILABLE(...bridgeos(x)...) which
# neither the SDK nor PD define.
target_compile_options(libc_static PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_SOURCE_DIR}/../libplatform/pd_bridgeos_compat.h>)
# xnu_private_headers for the pthread QoS chain (pthread/priority_private.h in
# usr/local/include). dyld links this alongside -isysroot too.
target_link_libraries(libc_static PRIVATE xnu_private_headers)
target_include_directories(libc_static PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}               # <os/assumes.h> (os/assumes.h), <secure/...>
    ${CMAKE_CURRENT_SOURCE_DIR}/darwin        # libc_private.h
    ${CMAKE_CURRENT_SOURCE_DIR}/fbsdcompat     # namespace.h / un-namespace.h
    ${CMAKE_CURRENT_SOURCE_DIR}/stdio/FreeBSD   # local.h
    ${CMAKE_CURRENT_SOURCE_DIR}/gen/FreeBSD     # telldir.h
    ${CMAKE_CURRENT_SOURCE_DIR}/locale          # xlocale_private.h
    ${CMAKE_CURRENT_SOURCE_DIR}/locale/FreeBSD   # setlocale.h + rune internals
    ${CMAKE_CURRENT_SOURCE_DIR}/stdtime/FreeBSD   # timelocal.h (locale chain)
    ${CMAKE_CURRENT_SOURCE_DIR}/../libplatform/private   # _simple.h (subsystem.c)
    ${CMAKE_CURRENT_SOURCE_DIR}/../pthread/private       # pthread/tsd_private.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../libsystem_kernel/private_include  # os/tsd.h
    # NB: do NOT add libc/include -- its sys/cdefs.h shadows the SDK's and pulls
    # in the generated libc-features.h; the SDK sysroot provides the std headers.
)

# The other half of the libc dyld needs: files that require Libc's INTERNAL
# headers (un-namespaced internal DIR/FILE structs, the rune-locale tables) and
# therefore CANNOT use the SDK sysroot like libc_static. Built the way Apple
# builds libc: -nostdinc against libc/include (the internal std headers) + the
# generated libc-features.h + fbsdcompat namespace wrappers, with sys/* and
# mach/* coming from the xnu kernel headers. Opt-in (EXCLUDE_FROM_ALL).
add_darwin_static_library(libc_internal_static)
set_target_properties(libc_internal_static PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(libc_internal_static PRIVATE
    gen/FreeBSD/opendir.c        # _opendir$INODE64
    gen/FreeBSD/readdir.c        # _readdir$INODE64 / _readdir_r$INODE64
    gen/FreeBSD/closedir.c       # _closedir
    gen/FreeBSD/telldir.c        # telldir/seekdir support used by the above
    gen/FreeBSD/getcwd.c         # _getcwd / ___private_getcwd
    gen/FreeBSD/getpagesize.c    # _getpagesize (pulled by telldir)
    stdlib/FreeBSD/reallocf.c    # _reallocf (pulled by telldir/getcwd)
    stdlib/FreeBSD/merge.c       # _mergesort (pulled by scandir/getcwd)
    locale/FreeBSD/table.c       # ___maskrune / __DefaultRuneLocale (C-locale rune table)
    gen/dirfd.c                  # _dirfd
)
target_compile_definitions(libc_internal_static PRIVATE __DARWIN_UNIX03=1 _FORTIFY_SOURCE=0 NO_FLOATING_POINT PR_13085474_CHECK=0)
target_include_directories(libc_internal_static PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/pd-compat-include   # reconstructed apfs/apfs_fsctl.h
    ${CMAKE_CURRENT_SOURCE_DIR}/../libplatform/include  # libkern/OSAtomic.h (locale refcounts)
    ${CMAKE_CURRENT_SOURCE_DIR}/include             # internal std headers (dirent.h w/ __dd_*)
    ${CMAKE_CURRENT_BINARY_DIR}/include/${PUREDARWIN_ARCH}   # generated libc-features.h
    ${CMAKE_CURRENT_SOURCE_DIR}                      # <os/...>, <secure/...>
    ${CMAKE_CURRENT_SOURCE_DIR}/darwin
    ${CMAKE_CURRENT_SOURCE_DIR}/fbsdcompat           # namespace.h / un-namespace.h
    ${CMAKE_CURRENT_SOURCE_DIR}/stdio                # xprintf_domain.h
    ${CMAKE_CURRENT_SOURCE_DIR}/gen/FreeBSD          # telldir.h
    ${CMAKE_CURRENT_SOURCE_DIR}/stdio/FreeBSD        # local.h (internal FILE)
    ${CMAKE_CURRENT_SOURCE_DIR}/locale
    ${CMAKE_CURRENT_SOURCE_DIR}/locale/FreeBSD
    ${CMAKE_CURRENT_SOURCE_DIR}/stdtime/FreeBSD       # timelocal.h (locale chain)
    ${CMAKE_CURRENT_SOURCE_DIR}/../libsystem_kernel/private_include
    ${CMAKE_CURRENT_SOURCE_DIR}/../libsystem_kernel/mach
    ${CMAKE_BINARY_DIR}/src/Libraries/libSystem/libsystem_kernel/public_include
    # <pthread.h> (the dir fns include it) + the whole sys/_pthread/_pthread_*.h
    # family that xnu's sys/types.h references (xnu_header_install omits them).
    ${CMAKE_CURRENT_SOURCE_DIR}/../pthread/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../pthread/include/pthread   # <pthread.h> itself
    ${CMAKE_CURRENT_SOURCE_DIR}/../pthread/private           # pthread/tsd_private.h (xlocale)
    ${CMAKE_CURRENT_SOURCE_DIR}/../libplatform/private       # _simple.h (os/assumes.h)
    ${CMAKE_CURRENT_SOURCE_DIR}/../libmalloc/include         # malloc/_malloc.h (stdlib.h)
)
target_link_libraries(libc_internal_static PRIVATE
    clang_bootstrap_headers      # stddef/stdarg/stdint under -nostdinc
    AvailabilityHeaders
    architecture_headers
    # xnu_private_headers FIRST (PRIVATE mach/* defines) then public.
    xnu_private_headers
    xnu_headers
)
add_dependencies(libc_internal_static libc_features libsystem_kernel_static)
# Force-includes (C only): bridgeos availability shim + the FreeBSD compat shim
# (_fbsd_compat_.h: __isthreaded, and the _close/_fstat/... -> public-name
# remapping the FreeBSD sources expect).
target_compile_options(libc_internal_static PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_SOURCE_DIR}/../libplatform/pd_bridgeos_compat.h>
    $<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_SOURCE_DIR}/fbsdcompat/_fbsd_compat_.h>
    $<$<COMPILE_LANGUAGE:C>:-idirafter${PUREDARWIN_MACOSX_SDK}/usr/include>)
# The dir sources use the short DIR field names (dd_fd/dd_lock/...); gen/__dirent.h
# maps them onto the internal __dd_* members. Force-include it for those files.
set_source_files_properties(
    gen/FreeBSD/opendir.c gen/FreeBSD/readdir.c
    gen/FreeBSD/closedir.c gen/FreeBSD/telldir.c
    PROPERTIES COMPILE_FLAGS "-include${CMAKE_CURRENT_SOURCE_DIR}/gen/__dirent.h")
