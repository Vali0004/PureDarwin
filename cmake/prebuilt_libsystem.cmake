set(_puredarwin_prebuilt_libsystem_root "${PUREDARWIN_PREBUILT_LIBSYSTEM_ROOT}")

set(_puredarwin_prebuilt_libsystem_b "${_puredarwin_prebuilt_libsystem_root}/usr/lib/libSystem.B.dylib")
set(_puredarwin_prebuilt_libsystem "${_puredarwin_prebuilt_libsystem_root}/usr/lib/libSystem.dylib")
set(_puredarwin_prebuilt_libdyld "${_puredarwin_prebuilt_libsystem_root}/usr/lib/system/libdyld.dylib")
set(_puredarwin_prebuilt_dyld "${_puredarwin_prebuilt_libsystem_root}/usr/lib/dyld")

foreach(_puredarwin_prebuilt_path
        "${_puredarwin_prebuilt_libsystem_b}"
        "${_puredarwin_prebuilt_libsystem}"
        "${_puredarwin_prebuilt_libdyld}"
        "${_puredarwin_prebuilt_dyld}")
    if(NOT EXISTS "${_puredarwin_prebuilt_path}")
        message(FATAL_ERROR "Missing prebuilt libSystem artifact: ${_puredarwin_prebuilt_path}")
    endif()
endforeach()

add_library(libSystem_B_stub SHARED IMPORTED GLOBAL)
set_target_properties(libSystem_B_stub PROPERTIES
    IMPORTED_LOCATION "${_puredarwin_prebuilt_libsystem_b}")

add_library(libdyld SHARED IMPORTED GLOBAL)
set_target_properties(libdyld PROPERTIES
    IMPORTED_LOCATION "${_puredarwin_prebuilt_libdyld}")

add_executable(dyld IMPORTED GLOBAL)
set_target_properties(dyld PROPERTIES
    IMPORTED_LOCATION "${_puredarwin_prebuilt_dyld}")
