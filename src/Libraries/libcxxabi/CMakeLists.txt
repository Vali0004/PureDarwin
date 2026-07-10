set(_libcxxabi  "${CMAKE_CURRENT_SOURCE_DIR}")
set(_libcxx     "${CMAKE_CURRENT_SOURCE_DIR}/../libcxx")
set(_libunwind  "${CMAKE_CURRENT_SOURCE_DIR}/../libunwind")
if(NOT EXISTS "${_libcxxabi}/src/cxa_exception.cpp")
    message(FATAL_ERROR "vendored libc++abi sources missing at ${_libcxxabi}/src \
(expected the vendored LLVM libcxxabi subtree checked in here).")
endif()

get_filename_component(_toolchain_bin "${CMAKE_AR}" DIRECTORY)

# SDK/toolchain selection. CMake's APPLE is true for a Darwin target even when
# the host is Linux, so use CMAKE_HOST_APPLE to distinguish native AppleClang
# from osxcross.
if(CMAKE_HOST_APPLE AND NOT CMAKE_CROSSCOMPILING)
    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-path
        OUTPUT_VARIABLE _sdk
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    find_program(_real_clang NAMES clang REQUIRED)

    set(_target_flags -arch "${PUREDARWIN_ARCH}")
else()
    # osxcross tools normally look like:
    #   x86_64-apple-darwin20.4-ar
    get_filename_component(_ar_name "${CMAKE_AR}" NAME)

    if(NOT _ar_name MATCHES "^(.+)-ar$")
        message(FATAL_ERROR
            "Cannot derive the osxcross target triple from CMAKE_AR='${CMAKE_AR}'."
        )
    endif()

    set(_triple "${CMAKE_MATCH_1}")

    if(DEFINED ENV{OSXCROSS_SDK})
        set(_sdk "$ENV{OSXCROSS_SDK}")
    endif()
    if(NOT _sdk)
        set(_sdk "${PUREDARWIN_MACOSX_SDK}")
    endif()

    set(_real_clang "${_toolchain_bin}/${_triple}-clang")
    if(NOT EXISTS "${_real_clang}")
        message(FATAL_ERROR
            "Expected the osxcross clang wrapper at '${_real_clang}'."
        )
    endif()

    set(_target_flags)
endif()

set(_config "${CMAKE_CURRENT_SOURCE_DIR}/config")   # __config_site, __assertion_handler

set(_abi_flags
    ${_target_flags}
    -isysroot ${_sdk}
    -std=c++17 -nostdinc++ -funwind-tables -fexceptions -fPIC -Os -DNDEBUG
    -I ${_config}
    -I ${_libcxxabi}/include
    -I ${_libcxxabi}/src
    -I ${_libcxx}/src
    -idirafter ${_libcxx}/include
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
    pd_bootstrap_runtime
)

set(_abi_objs "")
foreach(s ${_abi_srcs})
    set(_obj "${CMAKE_CURRENT_BINARY_DIR}/${s}.o")
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
        COMMENT "Building libc++abi ${s}.cpp (raw clang, vendored LLVM headers)"
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

if(NOT EXISTS "${_libunwind}/src/libunwind.cpp")
    message(FATAL_ERROR "vendored libunwind sources missing at ${_libunwind}/src.")
endif()

set(_uw_flags
    ${_target_flags}
    -isysroot ${_sdk}
    -nostdinc++ -funwind-tables -fPIC -Os -DNDEBUG
    -I ${_libunwind}/include
    -I ${_libunwind}/src
    -I ${_config}
    -idirafter ${_libcxx}/include   # <__libunwind_config.h> et al pull libcxx macros
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
        COMMAND "${_real_clang}" ${_uw_flags} -std=c++17 -fno-exceptions -fno-rtti
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
