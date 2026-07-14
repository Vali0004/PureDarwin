if(NOT DEFINED BUSYBOX_CONFIG)
    message(FATAL_ERROR "BUSYBOX_CONFIG is required")
endif()

file(READ "${BUSYBOX_CONFIG}" config_text)

function(set_config_y symbol)
    string(
        REGEX REPLACE
        "(^|\n)(# )?CONFIG_${symbol}(=y|=[^\n]*| is not set)"
        "\\1CONFIG_${symbol}=y"
        config_text
        "${config_text}"
    )

    if(NOT config_text MATCHES "(^|\n)CONFIG_${symbol}=y(\n|$)")
        string(APPEND config_text "\nCONFIG_${symbol}=y\n")
    endif()

    set(config_text "${config_text}" PARENT_SCOPE)
endfunction()

function(set_config_unset symbol)
    string(
        REGEX REPLACE
        "(^|\n)(# )?CONFIG_${symbol}(=y|=[^\n]*| is not set)"
        "\\1# CONFIG_${symbol} is not set"
        config_text
        "${config_text}"
    )

    if(NOT config_text MATCHES
       "(^|\n)# CONFIG_${symbol} is not set(\n|$)")
        string(APPEND config_text "\n# CONFIG_${symbol} is not set\n")
    endif()

    set(config_text "${config_text}" PARENT_SCOPE)
endfunction()

function(set_config_string symbol value)
    string(
        REPLACE
        "\\"
        "\\\\"
        escaped_value
        "${value}"
    )

    string(
        REPLACE
        "\""
        "\\\""
        escaped_value
        "${escaped_value}"
    )

    string(
        REGEX REPLACE
        "(^|\n)(# )?CONFIG_${symbol}(=y|=[^\n]*| is not set)"
        "\\1CONFIG_${symbol}=\"${escaped_value}\""
        config_text
        "${config_text}"
    )

    if(NOT config_text MATCHES
       "(^|\n)CONFIG_${symbol}=\"${escaped_value}\"(\n|$)")
        string(
            APPEND
            config_text
            "\nCONFIG_${symbol}=\"${escaped_value}\"\n"
        )
    endif()

    set(config_text "${config_text}" PARENT_SCOPE)
endfunction()

foreach(symbol IN ITEMS
    BUSYBOX
    SHOW_USAGE
    FEATURE_VERBOSE_USAGE
    FEATURE_BUFFERS_USE_MALLOC
    INSTALL_APPLET_DONT

    CAT
    CHMOD
    CP
    ECHO
    FALSE
    LN
    LS
    MKDIR
    MV
    PWD
    REBOOT
    READLINK
    REALPATH
    RM
    RMDIR
    SHUTDOWN
    SLEEP
    SYNC
    TEST
    TRUE
    TRUNCATE
    TTY
    UNAME
    WHOAMI

    BASENAME
    CMP
    DIFF
    DIRNAME
    EXPR
    HEAD
    TAIL
    WC
    CUT
    TR
    SORT
    UNIQ
    GREP
    SED
    FIND
    XARGS
    TOUCH
    DATE
    ENV
    ID
    PRINTF
    SEQ
    YES
    WHICH
    HOSTNAME
    DU
    DD
    MKNOD
    STAT
    STTY
    KILL
    CLEAR
    RESET

    AWK
    GUNZIP
    GZIP
    MORE
    PATCH
    VI

    SH_IS_ASH
    ASH
    FEATURE_PREFER_APPLETS
    FEATURE_SH_STANDALONE
    ASH_ECHO
    ASH_PRINTF
    ASH_TEST
    ASH_GETOPTS
)
    set_config_y("${symbol}")
endforeach()

foreach(symbol IN ITEMS
    STATIC
    PIE
    NOMMU
    FEATURE_INSTALLER
    FEATURE_AWK_LIBM

    # df.c unconditionally includes <mntent.h>, which glibc/Linux provide
    # but the Darwin SDK does not (macOS uses getmntinfo(3) instead).
    DF

    # tar_main() references data_extract_to_command() unconditionally (the
    # call is behind a runtime "if", not an #ifdef), and this build's CFLAGS
    # carry no optimization level, so dead-code elimination never removes
    # the reference even with FEATURE_TAR_TO_COMMAND off. Drop TAR itself
    # rather than chase that further.
    TAR
    FEATURE_TAR_TO_COMMAND

    # Console-tools applets beyond CLEAR/RESET use Linux-only vt/kbd ioctls
    # and headers (e.g. <sys/kd.h>) that do not exist in the Darwin SDK.
    # oldconfig's non-interactive blank-answer pass re-derives newly visible
    # menu entries from their Kconfig defaults (often "y"), so these must be
    # pinned off explicitly rather than left unset.
    CHVT
    DEALLOCVT
    DUMPKMAP
    FGCONSOLE
    KBD_MODE
    LOADFONT
    FEATURE_LOADFONT_PSF2
    FEATURE_LOADFONT_RAW
    LOADKMAP
    OPENVT
    RESIZE
    FEATURE_RESIZE_PRINT
    SETCONSOLE
    FEATURE_SETCONSOLE_LONG_OPTIONS
    SETFONT
    FEATURE_SETFONT_TEXTUAL_MAP
    SETKEYCODES
    SETLOGCONS
    SHOWKEY
)
    set_config_unset("${symbol}")
endforeach()

set_config_string(
    PREFIX
    "./_install"
)

set_config_string(
    EXTRA_CFLAGS
    "-fno-stack-protector -Wno-ignored-optimization-argument -Wno-string-plus-int"
)

set_config_string(
    EXTRA_LDFLAGS
    ""
)

set_config_string(
    EXTRA_LDLIBS
    ""
)

set_config_string(
    BUSYBOX_EXEC_PATH
    "/bin/busybox"
)

file(WRITE "${BUSYBOX_CONFIG}" "${config_text}")