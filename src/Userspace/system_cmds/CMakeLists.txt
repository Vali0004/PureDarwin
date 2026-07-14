function(add_system_cmd name)
    add_darwin_executable(${name} MACOSX_VERSION_MIN 11.0)

    get_target_property(_system_cmd_opts ${name} COMPILE_OPTIONS)
    if(_system_cmd_opts)
        list(REMOVE_ITEM _system_cmd_opts -nostdinc)
        set_target_properties(${name} PROPERTIES COMPILE_OPTIONS "${_system_cmd_opts}")
    endif()

    target_sources(${name} PRIVATE ${ARGN})
    target_compile_options(${name} PRIVATE
        -isysroot ${PUREDARWIN_MACOSX_SDK}
        -fno-builtin
        -fno-stack-protector
    )
    target_link_options(${name} PRIVATE
        -nostdlib
        -Wl,-L,$<TARGET_FILE_DIR:libSystem_B_stub>
        -Wl,-dylinker_install_name,/usr/lib/dyld
        -Wl,-platform_version,macos,11.0,11.5
        -lSystem
    )
    add_dependencies(${name} libSystem_B_stub dyld)
    install(TARGETS ${name} DESTINATION bin COMPONENT BaseSystem)
endfunction()

add_system_cmd(mkfile mkfile.c)
add_system_cmd(sync sync.c)
