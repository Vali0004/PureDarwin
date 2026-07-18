add_subdirectory(libmacho)
add_subdirectory(libstuff)
add_subdirectory(misc)
# add_subdirectory(otool) # disabled - doesn't build
add_subdirectory(ld64/src)
if(PUREDARWIN_ENABLE_SELFHOST_CCTOOLS)
    add_subdirectory(misc-selfhost)
endif()
