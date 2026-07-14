add_darwin_executable(sockettest MACOSX_VERSION_MIN 11.0)

get_target_property(_sockettest_opts sockettest COMPILE_OPTIONS)
if(_sockettest_opts)
    list(REMOVE_ITEM _sockettest_opts -nostdinc)
    set_target_properties(sockettest PROPERTIES COMPILE_OPTIONS "${_sockettest_opts}")
endif()

target_sources(sockettest PRIVATE
    sockettest.c
)

target_compile_options(sockettest PRIVATE
    -isysroot ${PUREDARWIN_MACOSX_SDK}
    -fno-builtin
    -fno-stack-protector
)

target_link_options(sockettest PRIVATE
    -nostdlib
    -Wl,-L,$<TARGET_FILE_DIR:libSystem_B_stub>
    -Wl,-dylinker_install_name,/usr/lib/dyld
    -Wl,-platform_version,macos,11.0,11.5
    -lSystem
)

add_dependencies(sockettest libSystem_B_stub dyld)

install(TARGETS sockettest DESTINATION bin COMPONENT BaseSystem)
