if(NOT PUREDARWIN_ARM64)
    add_subdirectory(AppleAPIC)
    add_subdirectory(AppleI386GenericPlatform)
    add_subdirectory(AppleI386PCI)
    add_subdirectory(AppleIntelPIIXATA)
    add_subdirectory(ApplePS2Controller)
    add_subdirectory(IOACPIFamily)
    add_subdirectory(IOATAFamily)
    add_subdirectory(IOATABlockStorage)
endif()

add_subdirectory(IOPCIFamily)
add_subdirectory(IOStorageFamily)
add_subdirectory(RavynAHCIPort)
add_subdirectory(RavynXHCIPort)
add_subdirectory(IOGOPFramebuffer)
add_subdirectory(IOHIDFamily)
# MiniRV32IMA #includes from the mini-rv32ima checkout expected NEXT TO this
# repo - an external tree the Nix sandbox (and any bare clone) doesn't have.
# It's optional (build-kc.sh only packs it with MINIRV32=1), so only build
# it when its dependency is actually present.
if(EXISTS "${CMAKE_SOURCE_DIR}/../mini-rv32ima/mini-rv32ima/mini-rv32ima.h")
    add_subdirectory(MiniRV32IMA)
    set(PUREDARWIN_HAVE_MINIRV32 TRUE)
endif()
add_subdirectory(corecrypto)
add_subdirectory(ext4)
add_subdirectory(HFSEncodings)
add_subdirectory(hfs)
add_subdirectory(AppleFileSystemDriver)
add_subdirectory(Ext4FileSystemDriver)
add_subdirectory(pthread)

# Umbrella target so builds that name explicit targets (e.g. the Nix full
# build - `ninja all` is unusable there, it drags in tools/* as bogus
# cross-compiled targets) can pull in every kext with one name.
add_custom_target(kexts)
add_dependencies(kexts
    IOPCIFamily ApplePIODMA IOStorageFamily
    RavynAHCIPort.kext RavynXHCIPort.kext
    IOGOPFramebuffer.kext IOHIDFamily.kext
    corecrypto.kext ext4.kext HFSEncodings.kext hfs.kext
    AppleFileSystemDriver.kext Ext4FileSystemDriver.kext pthread.kext
)
if(PUREDARWIN_HAVE_MINIRV32)
    add_dependencies(kexts MiniRV32IMA.kext)
endif()
if(NOT PUREDARWIN_ARM64)
    add_dependencies(kexts
        AppleAPIC AppleI386GenericPlatform AppleI386PCI AppleIntelPIIXATA
        ApplePS2Controller IOACPIFamily IOATAFamily IOATABlockStorage.kext
    )
endif()
