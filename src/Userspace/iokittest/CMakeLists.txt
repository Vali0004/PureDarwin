add_darwin_executable(iokittest MACOSX_VERSION_MIN 11.0)
add_darwin_executable(ioreg MACOSX_VERSION_MIN 11.0)

foreach(_pd_tgt iokittest ioreg)
    get_target_property(_pd_tgt_opts ${_pd_tgt} COMPILE_OPTIONS)
    if(_pd_tgt_opts)
        list(REMOVE_ITEM _pd_tgt_opts -nostdinc)
        set_target_properties(${_pd_tgt} PROPERTIES COMPILE_OPTIONS "${_pd_tgt_opts}")
    endif()

    target_compile_options(${_pd_tgt} PRIVATE
        -isysroot ${PUREDARWIN_MACOSX_SDK}
        -fno-builtin
        -fno-stack-protector
    )
endforeach()

target_sources(iokittest PRIVATE
    iokittest.c
)

target_sources(ioreg PRIVATE
    ioreg.c
)

if(TARGET PDGOP)
    target_link_libraries(iokittest PRIVATE PDGOP)
    target_link_libraries(ioreg PRIVATE IOKit)
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
    target_link_libraries(ioreg PRIVATE iokittest_IOKit)
    foreach(_pd_tgt iokittest ioreg)
        if(TARGET libsystem_kernel_static)
            target_link_libraries(${_pd_tgt} PRIVATE libsystem_kernel_static)
        endif()
        if(TARGET libsyscall_traps)
            target_link_libraries(${_pd_tgt} PRIVATE libsyscall_traps)
        endif()
    endforeach()
endif()

foreach(_pd_tgt iokittest ioreg)
    target_link_options(${_pd_tgt} PRIVATE
        -nostdlib
        -Wl,-L,$<TARGET_FILE_DIR:libSystem_B_stub>
        -Wl,-dylinker_install_name,/usr/lib/dyld
        -Wl,-platform_version,macos,11.0,11.5
    )

    target_link_libraries(${_pd_tgt} PRIVATE System)

    add_dependencies(${_pd_tgt} libSystem_B_stub dyld)

    install(TARGETS ${_pd_tgt} DESTINATION bin COMPONENT BaseSystem)
endforeach()
