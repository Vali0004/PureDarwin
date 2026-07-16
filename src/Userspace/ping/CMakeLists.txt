add_darwin_executable(ping MACOSX_VERSION_MIN 11.0)

get_target_property(_ping_opts ping COMPILE_OPTIONS)
if(_ping_opts)
    list(REMOVE_ITEM _ping_opts -nostdinc)
    set_target_properties(ping PROPERTIES COMPILE_OPTIONS "${_ping_opts}")
endif()

target_sources(ping PRIVATE
    ping.c
)

target_compile_options(ping PRIVATE
    -isysroot ${PUREDARWIN_MACOSX_SDK}
    -fno-builtin
    -fno-stack-protector
)

target_link_options(ping PRIVATE
    -nostdlib
    -Wl,-L,$<TARGET_FILE_DIR:libSystem_B_stub>
    -Wl,-dylinker_install_name,/usr/lib/dyld
    -Wl,-platform_version,macos,11.0,11.5
    -lSystem
)

add_dependencies(ping libSystem_B_stub dyld)

install(TARGETS ping DESTINATION bin COMPONENT BaseSystem)
