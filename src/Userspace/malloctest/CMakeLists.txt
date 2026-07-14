add_darwin_executable(malloctest MACOSX_VERSION_MIN 11.0)

get_target_property(_malloctest_opts malloctest COMPILE_OPTIONS)
if(_malloctest_opts)
    list(REMOVE_ITEM _malloctest_opts -nostdinc)
    set_target_properties(malloctest PROPERTIES COMPILE_OPTIONS "${_malloctest_opts}")
endif()

target_sources(malloctest PRIVATE
    malloctest.c
)

target_compile_options(malloctest PRIVATE
    -isysroot ${PUREDARWIN_MACOSX_SDK}
    -fno-builtin
    -fno-stack-protector
)

target_link_options(malloctest PRIVATE
    -nostdlib
    -Wl,-L,$<TARGET_FILE_DIR:libSystem_B_stub>
    -Wl,-dylinker_install_name,/usr/lib/dyld
    -Wl,-platform_version,macos,11.0,11.5
    -lSystem
)

add_dependencies(malloctest libSystem_B_stub dyld)

install(TARGETS malloctest DESTINATION bin COMPONENT BaseSystem)
