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

target_link_libraries(iokittest PRIVATE PDGOP)

target_link_options(iokittest PRIVATE
    -nostdlib
    -Wl,-L,$<TARGET_FILE_DIR:libSystem_B_stub>
    -Wl,-dylinker_install_name,/usr/lib/dyld
    -Wl,-platform_version,macos,11.0,11.5
    -lSystem
)

add_dependencies(iokittest libSystem_B_stub dyld)

install(TARGETS iokittest DESTINATION bin COMPONENT BaseSystem)
