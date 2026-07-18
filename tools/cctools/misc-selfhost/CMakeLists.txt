macro(cctools_selfhost_executable name)
    add_darwin_executable(${name}_selfhost MACOSX_VERSION_MIN 11.0)
    get_target_property(_selfhost_opts ${name}_selfhost COMPILE_OPTIONS)
    if(_selfhost_opts)
        list(REMOVE_ITEM _selfhost_opts -nostdinc)
        set_target_properties(${name}_selfhost PROPERTIES COMPILE_OPTIONS "${_selfhost_opts}")
    endif()
    target_sources(${name}_selfhost PRIVATE ../misc/${name}.c)
    target_include_directories(${name}_selfhost PRIVATE ../include ../include/foreign ../libstuff)
    target_compile_definitions(${name}_selfhost PRIVATE -D__DARWIN_UNIX03)
    target_compile_options(${name}_selfhost PRIVATE -isysroot ${PUREDARWIN_MACOSX_SDK})
    target_link_options(${name}_selfhost PRIVATE
        -nostdlib
        -Wl,-L,$<TARGET_FILE_DIR:libSystem_B_stub>
        -Wl,-dylinker_install_name,/usr/lib/dyld
        -Wl,-platform_version,macos,11.0,11.5
        -lSystem)
    target_link_libraries(${name}_selfhost PRIVATE libmacho_static libstuff_static)
    add_dependencies(${name}_selfhost libSystem_B_stub dyld)
    set_property(TARGET ${name}_selfhost PROPERTY OUTPUT_NAME ${name})
    install(TARGETS ${name}_selfhost DESTINATION usr/bin COMPONENT BaseSystem)
endmacro()

cctools_selfhost_executable(lipo)
cctools_selfhost_executable(size)
cctools_selfhost_executable(strings)
cctools_selfhost_executable(checksyms)
