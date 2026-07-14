add_darwin_executable(fbtri MACOSX_VERSION_MIN 11.0)

get_target_property(_fbtri_opts fbtri COMPILE_OPTIONS)
if(_fbtri_opts)
    list(REMOVE_ITEM _fbtri_opts -nostdinc)
    set_target_properties(fbtri PROPERTIES COMPILE_OPTIONS "${_fbtri_opts}")
endif()

target_sources(fbtri PRIVATE
    fbtri.c
)

target_compile_options(fbtri PRIVATE
    -isysroot ${PUREDARWIN_MACOSX_SDK}
    -fno-builtin
    -fno-stack-protector
)

target_link_options(fbtri PRIVATE
    -nostdlib
    -Wl,-L,$<TARGET_FILE_DIR:libSystem_B_stub>
    -Wl,-dylinker_install_name,/usr/lib/dyld
    -Wl,-platform_version,macos,11.0,11.5
    -lSystem
)

add_dependencies(fbtri libSystem_B_stub dyld)

install(TARGETS fbtri DESTINATION bin COMPONENT BaseSystem)
