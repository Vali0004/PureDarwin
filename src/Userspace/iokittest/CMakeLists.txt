add_darwin_executable(iokittest MACOSX_VERSION_MIN 11.0)

get_target_property(_iokittest_opts iokittest COMPILE_OPTIONS)
if(_iokittest_opts)
    list(REMOVE_ITEM _iokittest_opts -nostdinc)
    set_target_properties(iokittest PROPERTIES COMPILE_OPTIONS "${_iokittest_opts}")
endif()

target_sources(iokittest PRIVATE
    iokittest.c
)

target_compile_options(iokittest PRIVATE
    -isysroot ${PUREDARWIN_MACOSX_SDK}
    -fno-builtin
    -fno-stack-protector
)

if(TARGET PDGOP)
    target_link_libraries(iokittest PRIVATE PDGOP)
else()
    if(NOT TARGET migcom AND DEFINED ENV{NIX_MIGCOM_PATH})
        add_executable(migcom IMPORTED GLOBAL)
        set_target_properties(migcom PROPERTIES IMPORTED_LOCATION "$ENV{NIX_MIGCOM_PATH}")
    endif()

    add_library(iokittest_IOKit STATIC)
    mig(${CMAKE_SOURCE_DIR}/src/Kernel/xnu/osfmk/device/device.defs
        INCLUDES_FROM_TARGET iokittest_IOKit
        USER_HEADER ${CMAKE_CURRENT_BINARY_DIR}/device_user.h
        USER_SOURCE ${CMAKE_CURRENT_BINARY_DIR}/device_user.c
        NOVOUCHERS)
    target_sources(iokittest_IOKit PRIVATE
        ${CMAKE_SOURCE_DIR}/src/Libraries/IOKit/IOKitLib.c
        ${CMAKE_CURRENT_BINARY_DIR}/device_user.c
    )
    target_compile_definitions(iokittest_IOKit PRIVATE IOKIT=1 KERNEL=0)
    target_include_directories(iokittest_IOKit
        PUBLIC ${CMAKE_SOURCE_DIR}/src/Libraries/IOKit/include
        PRIVATE
            ${CMAKE_CURRENT_BINARY_DIR}
            ${CMAKE_SOURCE_DIR}/src/Libraries/libSystem/libsystem_kernel/mach
    )

    add_library(iokittest_PDGOP STATIC)
    target_sources(iokittest_PDGOP PRIVATE
        ${CMAKE_SOURCE_DIR}/src/Libraries/PDGOP/PDGOP.c
    )
    target_include_directories(iokittest_PDGOP
        PUBLIC
            ${CMAKE_SOURCE_DIR}/src/Libraries/PDGOP/include
            ${CMAKE_SOURCE_DIR}/src/Libraries/IOKit/include
        PRIVATE ${CMAKE_SOURCE_DIR}/src/Libraries/libSystem/libsystem_kernel/mach
    )
    target_link_libraries(iokittest PRIVATE iokittest_PDGOP iokittest_IOKit)
    if(TARGET libsystem_kernel_static)
        target_link_libraries(iokittest PRIVATE libsystem_kernel_static)
    endif()
    if(TARGET libsyscall_traps)
        target_link_libraries(iokittest PRIVATE libsyscall_traps)
    endif()
endif()

target_link_options(iokittest PRIVATE
    -nostdlib
    -Wl,-L,$<TARGET_FILE_DIR:libSystem_B_stub>
    -Wl,-dylinker_install_name,/usr/lib/dyld
    -Wl,-platform_version,macos,11.0,11.5
)

target_link_libraries(iokittest PRIVATE System)

add_dependencies(iokittest libSystem_B_stub dyld)

install(TARGETS iokittest DESTINATION bin COMPONENT BaseSystem)
