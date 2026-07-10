add_darwin_executable(helloapp MACOSX_VERSION_MIN 11.0)

get_target_property(_helloapp_opts helloapp COMPILE_OPTIONS)
if(_helloapp_opts)
    list(REMOVE_ITEM _helloapp_opts -nostdinc)
    set_target_properties(helloapp PROPERTIES COMPILE_OPTIONS "${_helloapp_opts}")
endif()

target_sources(helloapp PRIVATE
    helloapp.c
)

target_compile_options(helloapp PRIVATE
    -isysroot ${PUREDARWIN_MACOSX_SDK}
    -fno-builtin
    -fno-stack-protector
)

target_link_options(helloapp PRIVATE
    -nostdlib
    -Wl,-dylinker_install_name,/usr/lib/dyld
    -Wl,-platform_version,macos,11.0,11.5
    -lSystem
)

install(TARGETS helloapp DESTINATION bin COMPONENT BaseSystem)
