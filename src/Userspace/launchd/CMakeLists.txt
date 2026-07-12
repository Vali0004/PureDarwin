add_darwin_executable(launchd MACOSX_VERSION_MIN 11.0)

get_target_property(_launchd_opts launchd COMPILE_OPTIONS)
if(_launchd_opts)
    list(REMOVE_ITEM _launchd_opts -nostdinc)
    set_target_properties(launchd PROPERTIES COMPILE_OPTIONS "${_launchd_opts}")
endif()

target_sources(launchd PRIVATE
    init.c
)

target_compile_options(launchd PRIVATE
    -isysroot ${PUREDARWIN_MACOSX_SDK}
    -fno-builtin
    -fno-stack-protector
)

target_link_options(launchd PRIVATE
    -nostdlib
    -isysroot ${PUREDARWIN_MACOSX_SDK}
    -Wl,-L,$<TARGET_FILE_DIR:libSystem_B_stub>
    -Wl,-dylinker_install_name,/usr/lib/dyld
    -Wl,-platform_version,macos,11.0,11.5
    -lSystem
)

add_dependencies(launchd libSystem_B_stub dyld)

install(TARGETS launchd DESTINATION sbin COMPONENT BaseSystem)
