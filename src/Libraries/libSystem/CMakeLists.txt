foreach(_puredarwin_libsystem_dir
    corecrypto
    libc
    libplatform
    libmalloc
    libsystem_kernel
    pthread
    libdyld
    stub
)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_puredarwin_libsystem_dir}/CMakeLists.txt")
        add_subdirectory("${_puredarwin_libsystem_dir}")
    endif()
endforeach()
