{ stdenv
, lib
, requireFile
, cmake
, ninja
, darwinCrossToolchain
, nativeLd
, libSystem
, icu
, src
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
  pname = "puredarwin-corefoundation";
  version = "1338";

  inherit src;

  nativeBuildInputs = [ cmake ninja ];

  # Real Apple SDKs never publish unicode/*.h (ICU is a private/internal
  # dependency there too - Apple just doesn't ship its own build-time ICU
  # headers in the public SDK either), but they DO ship the library stub
  # (usr/lib/libicucore.tbd) matching what our SDK tarball has. ICU headers
  # are portable, pure C, need no cross-compilation - borrow nixpkgs' icu4c
  # dev headers and let this link against the SDK's libicucore.tbd, same as
  # real Darwin does.
  #
  # nixpkgs' icu4c headers default to ICU's "renamed" symbol scheme (every
  # export gets a _<major-version> suffix, e.g. ucnv_fromUCallback_76, so
  # multiple ICU versions can coexist on one system) - but Apple's internal
  # libicucore build has renaming disabled, so libicucore.tbd only has the
  # plain, unversioned names. U_DISABLE_RENAMING=1 makes our headers emit
  # the same plain names, matching what the SDK's stub actually exports.
  configurePhase = ''
    runHook preConfigure

    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"
    # cmake's try_compile() sub-invocations (used for compiler ABI/feature
    # detection) don't reliably inherit -D cache variables from the outer
    # configure - the toolchain file already falls back to reading these as
    # environment variables, so set those instead of relying only on -D.
    export NIX_DARWIN_TOOLCHAIN_DIR="${darwinCrossToolchain}/bin"

    cmake -S . -B build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=${../../cmake/nix-toolchain.cmake} \
      -DNIX_DARWIN_TOOLCHAIN_DIR=${darwinCrossToolchain}/bin \
      -DNIX_DARWIN_SDK_ROOT=$DARWIN_SDK_ROOT \
      -DBUILD_SHARED_LIBS=OFF \
      -DCMAKE_C_FLAGS="-isysroot $DARWIN_SDK_ROOT -I${libSystem}/usr/include -I${lib.getDev icu}/include -DU_DISABLE_RENAMING=1 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0" \
      -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=${nativeLd}/bin/ld"

    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    ninja -C build
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/lib $out/include
    cp build/libCoreFoundation.a $out/lib/
    cp -a build/CoreFoundation.framework/Headers/. $out/include/
    cp -a build/CoreFoundation.framework/PrivateHeaders/. $out/include/
    runHook postInstall
  '';

  dontFixup = true;

  meta = with lib; {
    description = "PureDarwin CoreFoundation (from apple/swift-corelibs-foundation), cross-built as a static library";
    platforms = platforms.linux;
  };
}
