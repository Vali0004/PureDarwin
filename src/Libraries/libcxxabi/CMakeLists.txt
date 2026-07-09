# libc++abi (Itanium C++ ABI) static subset for dyld.
#
# dyld links -nostdlib and pulls the C++ ABI runtime in statically: __cxa_throw,
# __cxa_begin/end_catch, operator new/delete, the personality routine, the
# std::*_error exception types and their typeinfo/vtables. These are the
# version-INDEPENDENT half of the C++ runtime (the Itanium ABI is stable), so we
# build them from the vendored LLVM 21 libc++abi even though dyld's own STL was
# compiled against the SDK 11.3 libc++. (The version-SENSITIVE STL half -- libc++
# proper -- is built separately.)
#
# The sources are VENDORED in-tree (no external checkout): the libc++abi/libc++/
# libunwind subtrees live under src/Libraries/{libcxxabi,libcxx,libunwind},
# committed so the build is reproducible. They must match the toolchain's clang
# (LLVM 21).
#
# Why a hand-rolled custom-command build instead of add_darwin_static_library:
# the osxcross clang++ *wrapper* unconditionally injects `-isystem
# <SDK>/usr/include/c++/v1` (the SDK's own libc++ headers). That can't be
# suppressed with -nostdinc++, and it shadows the vendored LLVM 21 libc++ headers
# we must compile against (LLVM 21 uses newer internal macros the SDK headers
# lack). The underlying real clang (same osxcross install, minus the wrapper) does
# NOT inject the SDK libc++, so we invoke it directly with explicit -target/-isysroot.

# In-tree vendored source roots.
set(_libcxxabi  "${CMAKE_CURRENT_SOURCE_DIR}")
set(_libcxx     "${CMAKE_CURRENT_SOURCE_DIR}/../libcxx")
set(_libunwind  "${CMAKE_CURRENT_SOURCE_DIR}/../libunwind")
if(NOT EXISTS "${_libcxxabi}/src/cxa_exception.cpp")
    message(FATAL_ERROR "vendored libc++abi sources missing at ${_libcxxabi}/src \
(expected the LLVM 21 libcxxabi subtree checked in here).")
endif()

# The real clang, bypassing the osxcross wrapper. It lives beside the toolchain's
# archiver; derive the bin dir from there so we don't hardcode an install prefix.
get_filename_component(_osxcross_bin "${CMAKE_AR}" DIRECTORY)
set(_real_clang "${_osxcross_bin}/clang")
if(NOT EXISTS "${_real_clang}")
    message(FATAL_ERROR "expected the un-wrapped clang at ${_real_clang}")
endif()

# SDK: the wrapper normally supplies -isysroot; the raw clang needs it explicitly.
if(DEFINED ENV{OSXCROSS_SDK})
    set(_sdk "$ENV{OSXCROSS_SDK}")
else()
    message(FATAL_ERROR "OSXCROSS_SDK not in environment; cannot locate the macOS SDK.")
endif()

# Target triple, from the toolchain's tool prefix (…/<triple>-ar).
get_filename_component(_ar_name "${CMAKE_AR}" NAME)
string(REGEX REPLACE "-ar$" "" _triple "${_ar_name}")

set(_config "${CMAKE_CURRENT_SOURCE_DIR}/config")   # __config_site, __assertion_handler

set(_abi_flags
    -target ${_triple} -arch ${PUREDARWIN_ARCH}
    -isysroot ${_sdk}
    -std=c++20 -nostdinc++ -funwind-tables -fexceptions -fPIC -Os -DNDEBUG
    -I ${_config}
    -I ${_libcxx}/include
    -I ${_libcxxabi}/include
    -I ${_libcxxabi}/src
    -I ${_libcxx}/src               # libcxxabi does #include "include/…" relative to here
    -D_LIBCXXABI_BUILDING_LIBRARY
    -D_LIBCPP_BUILDING_LIBRARY
    -DLIBCXX_BUILDING_LIBCXXABI
)

# The subset dyld actually needs. cxa_demangle/cxa_thread_atexit/cxa_vector/
# aix/noexception are deliberately excluded (not referenced by dyld).
set(_abi_srcs
    stdlib_new_delete
    cxa_aux_runtime
    cxa_handlers
    cxa_default_handlers
    cxa_exception
    cxa_exception_storage
    cxa_personality
    cxa_virtual
    cxa_guard
    private_typeinfo
    stdlib_typeinfo
    stdlib_exception
    stdlib_stdexcept
    fallback_malloc
    abort_message
)

set(_abi_objs "")
foreach(s ${_abi_srcs})
    set(_obj "${CMAKE_CURRENT_BINARY_DIR}/${s}.o")
    # Per-thread C++ exception globals: dyld's libpthread variant (VARIANT_DYLD)
    # has NO dynamic TLS keys -- pthread_key_create is absent and setspecific
    # returns ENOTSUP -- so the default pthread-key storage path in
    # cxa_exception_storage won't link. dyld is single-threaded at startup and
    # serializes dlopen under a global lock, so a single global __cxa_eh_globals
    # is correct here. Build just this file with _LIBCXXABI_HAS_NO_THREADS.
    set(_abi_extra "")
    if(s STREQUAL "cxa_exception_storage")
        set(_abi_extra -D_LIBCXXABI_HAS_NO_THREADS)
    endif()
    add_custom_command(
        OUTPUT "${_obj}"
        COMMAND "${_real_clang}" ${_abi_flags} ${_abi_extra}
                -c "${_libcxxabi}/src/${s}.cpp" -o "${_obj}"
        DEPENDS "${_libcxxabi}/src/${s}.cpp"
                "${_config}/__config_site" "${_config}/__assertion_handler"
        COMMENT "Building libc++abi ${s}.cpp (raw clang, vendored LLVM 21 headers)"
        VERBATIM
    )
    list(APPEND _abi_objs "${_obj}")
endforeach()

set(_abi_archive "${CMAKE_CURRENT_BINARY_DIR}/libcxxabi_static.a")
add_custom_command(
    OUTPUT "${_abi_archive}"
    COMMAND "${CMAKE_AR}" crs "${_abi_archive}" ${_abi_objs}
    DEPENDS ${_abi_objs}
    COMMENT "Archiving libcxxabi_static.a"
    VERBATIM
)
add_custom_target(libcxxabi_static_build DEPENDS "${_abi_archive}")

# Expose it as an IMPORTED archive so dyld can link it by target.
add_library(libcxxabi_static STATIC IMPORTED GLOBAL)
set_target_properties(libcxxabi_static PROPERTIES
    IMPORTED_LOCATION "${_abi_archive}")
add_dependencies(libcxxabi_static libcxxabi_static_build)

# --------------------------------------------------------------------------
# libunwind: the _Unwind_* level-1/2 EH machinery that libc++abi's personality
# routine (___gxx_personality_v0) and ___cxa_throw drive. Same LLVM 21 tree,
# same raw-clang mechanism (the wrapper's SDK libc++ injection is irrelevant
# here since libunwind is nearly freestanding, but we reuse the raw clang for
# consistency and to keep -nostdinc++ clean).
if(NOT EXISTS "${_libunwind}/src/libunwind.cpp")
    message(FATAL_ERROR "vendored libunwind sources missing at ${_libunwind}/src.")
endif()

set(_uw_flags
    -target ${_triple} -arch ${PUREDARWIN_ARCH}
    -isysroot ${_sdk}
    -nostdinc++ -funwind-tables -fPIC -Os -DNDEBUG
    -I ${_libunwind}/include
    -I ${_libunwind}/src
    -I ${_config} -I ${_libcxx}/include   # <__libunwind_config.h> et al pull libcxx macros
    -D_LIBUNWIND_IS_NATIVE_ONLY
    -D_LIBUNWIND_BUILDING_LIBUNWIND=1
)

# x86_64 Itanium/DWARF unwinder subset. Excluded: Unwind-sjlj.c (SjLj EH),
# Unwind-EHABI (ARM32), Unwind-seh (Windows), Unwind-wasm, Unwind_AIXExtras.
set(_uw_cxx    libunwind)
set(_uw_c      UnwindLevel1 UnwindLevel1-gcc-ext)
set(_uw_asm    UnwindRegistersRestore UnwindRegistersSave)

set(_uw_objs "")
foreach(s ${_uw_cxx})
    set(_obj "${CMAKE_CURRENT_BINARY_DIR}/uw_${s}.o")
    add_custom_command(OUTPUT "${_obj}"
        COMMAND "${_real_clang}" ${_uw_flags} -std=c++20 -fno-exceptions -fno-rtti
                -c "${_libunwind}/src/${s}.cpp" -o "${_obj}"
        DEPENDS "${_libunwind}/src/${s}.cpp"
        COMMENT "Building libunwind ${s}.cpp (raw clang)" VERBATIM)
    list(APPEND _uw_objs "${_obj}")
endforeach()
foreach(s ${_uw_c})
    set(_obj "${CMAKE_CURRENT_BINARY_DIR}/uw_${s}.o")
    add_custom_command(OUTPUT "${_obj}"
        COMMAND "${_real_clang}" ${_uw_flags} -std=c11
                -c "${_libunwind}/src/${s}.c" -o "${_obj}"
        DEPENDS "${_libunwind}/src/${s}.c"
        COMMENT "Building libunwind ${s}.c (raw clang)" VERBATIM)
    list(APPEND _uw_objs "${_obj}")
endforeach()
foreach(s ${_uw_asm})
    set(_obj "${CMAKE_CURRENT_BINARY_DIR}/uw_${s}.o")
    add_custom_command(OUTPUT "${_obj}"
        COMMAND "${_real_clang}" ${_uw_flags}
                -c "${_libunwind}/src/${s}.S" -o "${_obj}"
        DEPENDS "${_libunwind}/src/${s}.S"
        COMMENT "Building libunwind ${s}.S (raw clang)" VERBATIM)
    list(APPEND _uw_objs "${_obj}")
endforeach()

set(_uw_archive "${CMAKE_CURRENT_BINARY_DIR}/libunwind_static.a")
add_custom_command(OUTPUT "${_uw_archive}"
    COMMAND "${CMAKE_AR}" crs "${_uw_archive}" ${_uw_objs}
    DEPENDS ${_uw_objs}
    COMMENT "Archiving libunwind_static.a" VERBATIM)
add_custom_target(libunwind_static_build DEPENDS "${_uw_archive}")

add_library(libunwind_static STATIC IMPORTED GLOBAL)
set_target_properties(libunwind_static PROPERTIES
    IMPORTED_LOCATION "${_uw_archive}")
add_dependencies(libunwind_static libunwind_static_build)
