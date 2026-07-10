set(PUREDARWIN_BUSYBOX_SOURCE_DIR "${CMAKE_SOURCE_DIR}/../busybox-1.38.0")
set(PUREDARWIN_BUSYBOX_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/build")
set(PUREDARWIN_BUSYBOX_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/busybox-minimal.config")
set(PUREDARWIN_BUSYBOX_APPLY_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/apply-minimal-config.cmake")
set(PUREDARWIN_BUSYBOX_BIN "${PUREDARWIN_BUSYBOX_BUILD_DIR}/busybox")
find_program(PUREDARWIN_MAKE_PROGRAM NAMES gmake make REQUIRED)

add_custom_command(
    OUTPUT "${PUREDARWIN_BUSYBOX_BUILD_DIR}/.config"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${PUREDARWIN_BUSYBOX_BUILD_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${PUREDARWIN_BUSYBOX_SOURCE_DIR}/scripts/kconfig/zconf.tab.c_shipped"
        "${PUREDARWIN_BUSYBOX_SOURCE_DIR}/scripts/kconfig/zconf.tab.c"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${PUREDARWIN_BUSYBOX_SOURCE_DIR}/scripts/kconfig/lex.zconf.c_shipped"
        "${PUREDARWIN_BUSYBOX_SOURCE_DIR}/scripts/kconfig/lex.zconf.c"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${PUREDARWIN_BUSYBOX_SOURCE_DIR}/scripts/kconfig/zconf.hash.c_shipped"
        "${PUREDARWIN_BUSYBOX_SOURCE_DIR}/scripts/kconfig/zconf.hash.c"
    COMMAND "${CMAKE_COMMAND}" -E env
        "KCONFIG_ALLCONFIG=${PUREDARWIN_BUSYBOX_CONFIG}"
        "${PUREDARWIN_MAKE_PROGRAM}" -C "${PUREDARWIN_BUSYBOX_SOURCE_DIR}" O="${PUREDARWIN_BUSYBOX_BUILD_DIR}" allnoconfig
    COMMAND "${CMAKE_COMMAND}" -D "BUSYBOX_CONFIG=${PUREDARWIN_BUSYBOX_BUILD_DIR}/.config" -P "${PUREDARWIN_BUSYBOX_APPLY_CONFIG}"
    DEPENDS "${PUREDARWIN_BUSYBOX_CONFIG}" "${PUREDARWIN_BUSYBOX_APPLY_CONFIG}"
    VERBATIM
)

add_custom_command(
    OUTPUT "${PUREDARWIN_BUSYBOX_BIN}"
    COMMAND "${PUREDARWIN_MAKE_PROGRAM}"
        -C "${PUREDARWIN_BUSYBOX_SOURCE_DIR}"
        O="${PUREDARWIN_BUSYBOX_BUILD_DIR}"
        CC="${CMAKE_C_COMPILER}"
        HOSTCC="cc"
        CFLAGS="-fno-stack-protector"
        AR="${CMAKE_AR}"
        RANLIB="${CMAKE_RANLIB}"
        NM="${CMAKE_NM}"
        SKIP_STRIP=y
        busybox
    DEPENDS "${PUREDARWIN_BUSYBOX_BUILD_DIR}/.config"
    VERBATIM
)

add_custom_target(busybox ALL DEPENDS "${PUREDARWIN_BUSYBOX_BIN}")

install(PROGRAMS "${PUREDARWIN_BUSYBOX_BIN}" DESTINATION bin COMPONENT BaseSystem)
