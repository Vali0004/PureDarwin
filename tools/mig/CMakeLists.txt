set(MIGCOM_SRCS
    error.c
    global.c
    header.c
    mig.c
    routine.c
    server.c
    statement.c
    string.c
    type.c
    user.c
    utils.c

    ${CMAKE_CURRENT_BINARY_DIR}/y.tab.c
    ${CMAKE_CURRENT_BINARY_DIR}/lexxer.yy.c
)

add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/y.tab.c
    COMMAND yacc -d -b "${CMAKE_CURRENT_BINARY_DIR}/y" ${CMAKE_CURRENT_SOURCE_DIR}/parser.y
    COMMENT "Yacc parser.y" VERBATIM
)
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/lexxer.yy.c
    COMMAND lex -o "${CMAKE_CURRENT_BINARY_DIR}/lexxer.yy.c" ${CMAKE_CURRENT_SOURCE_DIR}/lexxer.l
    COMMENT "Lex lexxer.l" VERBATIM
)

if(CMAKE_HOST_APPLE)
    add_executable(migcom)
    target_sources(migcom PRIVATE ${MIGCOM_SRCS})
    target_compile_definitions(migcom PRIVATE MIG_VERSION=\"\")
    target_include_directories(migcom PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
else()
    # migcom is a pure source-to-source generator (MIG stub .c files); it
    # never needs to be Mach-O, only to actually RUN during the build. But
    # add_executable() here would inherit the project's global cross-
    # compiler like everything else, making it one anyway - a real
    # Canadian-cross gap, not specific to any particular cross toolchain
    # (confirmed: even a from-source osxcross build produces a Mach-O
    # migcom, equally unable to execute on the Linux build host). CMake
    # has no supported way to give one target a different compiler than
    # the rest of the project within a single configure, so build it with
    # raw add_custom_command calls against whatever "cc" real native
    # compiler is on PATH instead - correct on any Linux host regardless
    # of which cross toolchain (osxcross or this one) is otherwise in use.
    find_program(MIGCOM_NATIVE_CC NAMES cc gcc clang REQUIRED)

    set(_migcom_objs "")
    foreach(_src ${MIGCOM_SRCS})
        # get_filename_component(... ABSOLUTE) resolves plain filenames
        # ("error.c") against CMAKE_CURRENT_SOURCE_DIR while leaving the
        # two already-absolute generated sources (y.tab.c, lexxer.yy.c,
        # under CMAKE_CURRENT_BINARY_DIR) unchanged - custom commands run
        # from the build directory, not the source directory, so a bare
        # relative filename resolves nowhere.
        get_filename_component(_src_abs "${_src}" ABSOLUTE)
        get_filename_component(_name "${_src}" NAME_WE)
        set(_obj "${CMAKE_CURRENT_BINARY_DIR}/migcom_native/${_name}.o")
        add_custom_command(OUTPUT "${_obj}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/migcom_native"
            # mig's own sources #include <mach/boolean.h> etc, and rely on
            # u_int/u_short/etc BSD typedefs normally pulled in
            # transitively - force sys/types.h so those are always
            # defined regardless of header include order. Point at the
            # SDK's bundled copies (same ones real osxcross/macOS builds
            # resolve these from) rather than xnu's raw osfmk/bsd source
            # trees: those have their own header-generation steps (e.g.
            # bsd/sys/cdefs.h wants a build-generated
            # sys/_symbol_aliasing.h) that create a chicken-and-egg
            # ordering problem for a tool needed early in that same
            # pipeline. The SDK's headers are pre-generated and self-
            # contained, sidestepping that entirely.
            COMMAND "${MIGCOM_NATIVE_CC}" -DMIG_VERSION=\"\"
                    -I "${CMAKE_CURRENT_SOURCE_DIR}" -I "${CMAKE_CURRENT_BINARY_DIR}"
                    -I "${PUREDARWIN_MACOSX_SDK}/usr/include"
                    -include sys/types.h
                    -c "${_src_abs}" -o "${_obj}"
            DEPENDS "${_src_abs}"
            COMMENT "Building native migcom: ${_name}.c"
            VERBATIM
        )
        list(APPEND _migcom_objs "${_obj}")
    endforeach()

    # Built as migcom_native/migcom.native, deliberately NOT named exactly
    # "migcom" anywhere on disk: CMake's dependency resolution otherwise
    # conflates a custom_target's DEPENDS file path with a same-named
    # IMPORTED target's IMPORTED_LOCATION, producing a bogus self-cycle
    # ("migcom_build depends on migcom_build") at generate time.
    set(_migcom_bin "${CMAKE_CURRENT_BINARY_DIR}/migcom_native/migcom.native")
    add_custom_command(OUTPUT "${_migcom_bin}"
        COMMAND "${MIGCOM_NATIVE_CC}" -o "${_migcom_bin}" ${_migcom_objs}
        DEPENDS ${_migcom_objs}
        COMMENT "Linking native migcom"
        VERBATIM
    )
    # Other CMakeLists (cmake/mig.cmake, libsystem_kernel, ...) reference
    # $<TARGET_FILE:migcom>, which only resolves for a real executable/
    # library target, not a custom_target - so migcom itself has to stay
    # an (IMPORTED) executable target pointing at the binary a separate
    # custom_target actually builds.
    add_custom_target(migcom_build ALL DEPENDS "${_migcom_bin}")
    add_executable(migcom IMPORTED GLOBAL)
    set_property(TARGET migcom PROPERTY IMPORTED_LOCATION "${_migcom_bin}")
    add_dependencies(migcom migcom_build)
endif()

add_executable(mig IMPORTED)
set_property(TARGET mig PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/mig.sh)
add_dependencies(mig migcom)
