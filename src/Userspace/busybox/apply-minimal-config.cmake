if(NOT DEFINED BUSYBOX_CONFIG)
    message(FATAL_ERROR "BUSYBOX_CONFIG is required")
endif()

file(READ "${BUSYBOX_CONFIG}" config_text)

function(set_config_y symbol)
    string(REGEX REPLACE "(^|\n)(# )?CONFIG_${symbol}(=y|=.*| is not set)" "\\1CONFIG_${symbol}=y" config_text "${config_text}")
    if(NOT config_text MATCHES "(^|\n)CONFIG_${symbol}=y(\n|$)")
        string(APPEND config_text "\nCONFIG_${symbol}=y\n")
    endif()
    set(config_text "${config_text}" PARENT_SCOPE)
endfunction()

foreach(symbol IN ITEMS
    BUSYBOX
    SHOW_USAGE
    FEATURE_VERBOSE_USAGE
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
    RM
    RMDIR
    SHUTDOWN
    SLEEP
    TEST
    TRUE
    TRUNCATE
    UNAME
    BASENAME
    DIRNAME
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
    KILL
    CLEAR
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

file(WRITE "${BUSYBOX_CONFIG}" "${config_text}")
