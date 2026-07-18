{ stdenv
, lib
, requireFile
, darwinCrossToolchain
, nativeLd
, libSystem
, corefoundation
}:

let
  sdkTarball = requireFile {
    name = "MacOSX11.3.sdk.tar.xz";
    sha256 = "9adc1373d3879e1973d28ad9f17c9051b02931674a3ec2a2498128989ece2cb1";
    message = ''
      MacOSX11.3.sdk.tar.xz (Apple SDK, proprietary - not fetchable/redistributable)
      is not yet in your Nix store. Register your local copy with:
        nix-store --add-fixed sha256 /path/to/MacOSX11.3.sdk.tar.xz
    '';
  };
in
stdenv.mkDerivation {
  pname = "puredarwin-iokit";
  version = "0.1";

  dontUnpack = true;

  # Just the final dylib link step: libIOKitCF.a (src/Libraries/IOKit/
  # IOKitLibCF.c, real MIG-based IOServiceGetMatchingService/
  # IORegistryEntryCreateCFProperty/etc - see that file's own comment)
  # already got compiled as part of the main in-tree libSystem build
  # (which has the XNU-header/MIG machinery this needs), deliberately
  # without CoreFoundation's real headers. Link it against real CF here,
  # matching every other "-lCoreFoundation" consumer (fastfetch.nix etc).
  buildPhase = ''
    runHook preBuild

    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"

    ${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-clang \
      -isysroot "$DARWIN_SDK_ROOT" -dynamiclib \
      -fuse-ld=${nativeLd}/bin/ld -nostdlib \
      -L${libSystem}/usr/lib -L${corefoundation}/usr/lib \
      -Wl,-platform_version,macos,11.0,11.5 \
      -Wl,-install_name,/usr/lib/libIOKitCF.dylib \
      -Wl,-force_load,${libSystem}/usr/lib/system/libIOKitCF.a \
      -lCoreFoundation -lSystem \
      -o libIOKitCF.dylib

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/usr/lib
    cp libIOKitCF.dylib $out/usr/lib/
    runHook postInstall
  '';

  dontFixup = true;

  meta = with lib; {
    description = "Real CF-shaped IOKitLib (IOServiceGetMatchingService, IORegistryEntryCreateCFProperty, etc), linked against real CoreFoundation";
    platforms = platforms.linux;
  };
}
