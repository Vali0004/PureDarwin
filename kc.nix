{ stdenv, lib, kcTools, baseSystem, classic ? false }:

stdenv.mkDerivation {
  pname = "puredarwin-kc";
  version = "0.1";

  dontUnpack = true;

  buildPhase = ''
    runHook preBuild
    KEXTS=${baseSystem}/System/Library/Extensions

    codeless=()
    for p in "$KEXTS"/System.kext/PlugIns/*.kext; do
      codeless+=( -codeless "$p" )
    done

    ${kcTools}/bin/kc-builder \
      -kernel ${baseSystem}/System/Library/Kernels/kernel.debug \
      -kext "$KEXTS/corecrypto.kext" \
      -kext "$KEXTS/pthread.kext" \
      -kext "$KEXTS/IOACPIFamily.kext" \
      -kext "$KEXTS/IOPCIFamily.kext" \
      -kext "$KEXTS/AppleAPIC.kext" \
      -kext "$KEXTS/AppleI386GenericPlatform.kext" \
      -kext "$KEXTS/AppleI386PCI.kext" \
      -kext "$KEXTS/IOStorageFamily.kext" \
      -kext "$KEXTS/IOATAFamily.kext" \
      -kext "$KEXTS/IOATAFamily.kext/Contents/PlugIns/AppleIntelPIIXATA.kext" \
      -kext "$KEXTS/IOATABlockStorage.kext" \
      -kext "$KEXTS/ext4.kext" \
      -kext "$KEXTS/HFSEncodings.kext" \
      -kext "$KEXTS/hfs.kext" \
      -kext "$KEXTS/AppleFileSystemDriver.kext" \
      -kext "$KEXTS/Ext4FileSystemDriver.kext" \
      -kext "$KEXTS/IOHIDFamily.kext" \
      -kext "$KEXTS/RavynAHCIPort.kext" \
      -kext "$KEXTS/RavynXHCIPort.kext" \
      -kext "$KEXTS/IOGOPFramebuffer.kext" \
      -kext "$KEXTS/ApplePS2Controller.kext" \
      "''${codeless[@]}" \
      ${lib.optionalString classic "-classic"} \
      -o kernel
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out
    cp kernel $out/kernel
    runHook postInstall
  '';

  meta = with lib; {
    description = "PureDarwin boot kernel collection (kernel + kexts fileset), assembled by kc-tools' kc-builder";
    platforms = platforms.linux;
  };
}
