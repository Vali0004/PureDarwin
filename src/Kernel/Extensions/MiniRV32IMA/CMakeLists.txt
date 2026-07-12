add_kext_bundle(MiniRV32IMA.kext INFO_PLIST Info.plist KERNEL_PRIVATE
    BUNDLE_IDENTIFIER com.apple.kec.MiniRV32IMA BUNDLE_VERSION 0.1
    MAIN_FUNCTION MiniRV32IMA_start ANTIMAIN_FUNCTION MiniRV32IMA_stop
)
set_property(TARGET MiniRV32IMA.kext PROPERTY OUTPUT_NAME MiniRV32IMA)

target_sources(MiniRV32IMA.kext PRIVATE
    MiniRV32IMA.c
)

target_include_directories(MiniRV32IMA.kext PRIVATE
    ${CMAKE_SOURCE_DIR}/../mini-rv32ima/mini-rv32ima
)
target_link_libraries(MiniRV32IMA.kext PRIVATE xnu_kernel_private_headers)
target_compile_options(MiniRV32IMA.kext PRIVATE
    -Wno-cast-align
    -Wno-cast-qual
    -Wno-format
    -Wno-sign-conversion
    -Wno-implicit-int-conversion
    -Wno-unused-function
    -Wno-unused-variable
)
install(TARGETS MiniRV32IMA.kext DESTINATION System/Library/Extensions COMPONENT BaseSystem)
