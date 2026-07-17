macro(cctools_misc_executable name)
    add_executable(host_${name} ${name}.c)
    set_property(TARGET host_${name} PROPERTY OUTPUT_NAME ${name})
    target_link_libraries(host_${name} PRIVATE host_libmacho host_libstuff)
    target_include_directories(host_${name} PRIVATE ../include ../include/foreign ../libstuff)
    target_compile_definitions(host_${name} PRIVATE -D__DARWIN_UNIX03)
endmacro()

cctools_misc_executable(checksyms)
cctools_misc_executable(lipo)
cctools_misc_executable(size)
cctools_misc_executable(strings)
cctools_misc_executable(nm)
cctools_misc_executable(libtool)
cctools_misc_executable(redo_prebinding)
cctools_misc_executable(seg_addr_table)
cctools_misc_executable(seg_hack)
cctools_misc_executable(install_name_tool)
cctools_misc_executable(indr)
cctools_misc_executable(strip)
cctools_misc_executable(segedit)
cctools_misc_executable(pagestuff)
cctools_misc_executable(codesign_allocate)
cctools_misc_executable(bitcode_strip)
target_compile_definitions(host_bitcode_strip PRIVATE -DALLOW_ARCHIVES)
cctools_misc_executable(ctf_insert)
cctools_misc_executable(check_dylib)
cctools_misc_executable(cmpdylib)
cctools_misc_executable(inout)
cctools_misc_executable(vtool)

suppress_all_warnings(host_bitcode_strip)
suppress_all_warnings(host_install_name_tool)
suppress_all_warnings(host_strip)
suppress_all_warnings(host_vtool)

# Special cases
add_executable(host_nmedit strip.c)
target_link_libraries(host_nmedit PRIVATE host_libmacho host_libstuff)
target_include_directories(host_nmedit PRIVATE ../include ../include/foreign ../libstuff)
target_compile_definitions(host_nmedit PRIVATE -D__DARWIN_UNIX03 -DNMEDIT)

add_executable(host_ranlib libtool.c)
target_link_libraries(host_ranlib PRIVATE host_libmacho host_libstuff)
target_include_directories(host_ranlib PRIVATE ../include ../include/foreign ../libstuff)
target_compile_definitions(host_ranlib PRIVATE -D__DARWIN_UNIX03 -DRANLIB)

# Real x86_64-apple-darwin builds of the same tools, meant to eventually run
# ON PureDarwin itself. Starting with the subset that needs nothing beyond
# libSystem and libmacho_static/libstuff_static (no xar, no crypto, no C++):
# lipo, size, strings, checksyms. nm.c needs <xar/xar.h> and the rest of misc
# needs codesigning/crypto or C++ support we don't have cross-compiled yet -
# see docs/XNU_PATCHES.md-style notes once this grows further.
if(PUREDARWIN_ENABLE_SELFHOST_CCTOOLS)
    macro(cctools_selfhost_executable name)
        add_darwin_executable(${name} MACOSX_VERSION_MIN 11.0)
        get_target_property(_selfhost_opts ${name} COMPILE_OPTIONS)
        if(_selfhost_opts)
            list(REMOVE_ITEM _selfhost_opts -nostdinc)
            set_target_properties(${name} PROPERTIES COMPILE_OPTIONS "${_selfhost_opts}")
        endif()
        target_sources(${name} PRIVATE ${name}.c)
        target_include_directories(${name} PRIVATE ../include ../include/foreign ../libstuff)
        target_compile_definitions(${name} PRIVATE -D__DARWIN_UNIX03)
        target_compile_options(${name} PRIVATE -isysroot ${PUREDARWIN_MACOSX_SDK})
        target_link_options(${name} PRIVATE
            -nostdlib
            -Wl,-L,$<TARGET_FILE_DIR:libSystem_B_stub>
            -Wl,-dylinker_install_name,/usr/lib/dyld
            -Wl,-platform_version,macos,11.0,11.5
            -lSystem)
        target_link_libraries(${name} PRIVATE libmacho_static libstuff_static)
        add_dependencies(${name} libSystem_B_stub dyld)
        install(TARGETS ${name} DESTINATION usr/bin COMPONENT BaseSystem)
    endmacro()

    cctools_selfhost_executable(lipo)
    cctools_selfhost_executable(size)
    cctools_selfhost_executable(strings)
    cctools_selfhost_executable(checksyms)
endif()
