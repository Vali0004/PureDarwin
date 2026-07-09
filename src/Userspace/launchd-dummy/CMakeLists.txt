add_darwin_executable(launchd-dummy MACOSX_VERSION_MIN 11.0)
set_target_properties(launchd-dummy PROPERTIES
    EXCLUDE_FROM_ALL TRUE
    OUTPUT_NAME launchd)

target_sources(launchd-dummy PRIVATE launchd.c)

target_compile_options(launchd-dummy PRIVATE
    -isysroot ${PUREDARWIN_MACOSX_SDK}
    -fno-builtin
    -fno-stack-protector)

target_link_options(launchd-dummy PRIVATE
    -Wl,-e,__start
    # NOT -Wl,-dylinker -- that flag marks the OUTPUT itself as MH_DYLINKER
    # (dyld's own type; only one such binary may exist system-wide). A normal
    # MH_EXECUTE just needs its LC_LOAD_DYLINKER to name /usr/lib/dyld, which
    # -dylinker_install_name sets (matching dyld's own install path -- where
    # we actually install PD's dyld -- not the build-tree path).
    -Wl,-dylinker_install_name,/usr/lib/dyld
    -Wl,-platform_version,macos,11.0,11.0
    # ld64 requires a dynamic MH_EXECUTE to link libSystem.dylib, even though
    # launchd.c never calls into it (raw syscalls only) -- the SDK's stub .tbd
    # satisfies the linker without pulling in any actual libSystem code.
    -lSystem)

add_dependencies(launchd-dummy dyld)

install(TARGETS launchd-dummy RUNTIME DESTINATION bin COMPONENT BaseSystem)
