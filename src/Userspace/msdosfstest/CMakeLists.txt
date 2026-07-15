add_darwin_executable(msdosfstest MACOSX_VERSION_MIN 11.0)

get_target_property(_msdosfstest_opts msdosfstest COMPILE_OPTIONS)
if(_msdosfstest_opts)
    list(REMOVE_ITEM _msdosfstest_opts -nostdinc)
    set_target_properties(msdosfstest PROPERTIES COMPILE_OPTIONS "${_msdosfstest_opts}")
endif()

target_sources(msdosfstest PRIVATE
    msdosfstest.c
)

target_compile_options(msdosfstest PRIVATE
    -isysroot ${PUREDARWIN_MACOSX_SDK}
    -fno-builtin
    -fno-stack-protector
)

target_link_options(msdosfstest PRIVATE
    -nostdlib
    -Wl,-L,$<TARGET_FILE_DIR:libSystem_B_stub>
    -Wl,-dylinker_install_name,/usr/lib/dyld
    -Wl,-platform_version,macos,11.0,11.5
    -lSystem
)

add_dependencies(msdosfstest libSystem_B_stub dyld)

install(TARGETS msdosfstest DESTINATION bin COMPONENT BaseSystem)
