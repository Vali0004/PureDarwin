{ stdenv
, lib
, cmake
, ninja
, requireFile
, darwinCrossToolchain
, nativeLd
, nativeUnifdef
, nativeMigcom
, openssl
, bison
, flex
, perl
, bash
, zlib
, ed
, unifdef
, tcsh
, gnustep-base
, pax
, coreutils
, findutils
, gawk
, gnused
, clang
, libuuid
, ruby
, iig
, pname ? "puredarwin-nix-toolchain"
, buildTargets ? [ "helloapp" "launchd" ]
, installUserland ? true
, installKernel ? false
, installXnuHeaders ? false
, installKexts ? false
, installBaseSystem ? false
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
  inherit pname;
  version = "0.1";

  src = ./.;

  nativeBuildInputs = [
    cmake ninja darwinCrossToolchain bison flex perl bash ed unifdef tcsh
    gnustep-base pax coreutils findutils gawk gnused clang ruby iig
  ];

  buildInputs = [ zlib libuuid openssl ];

  NIX_DARWIN_TOOLCHAIN_DIR = "${darwinCrossToolchain}/bin";

  configurePhase = ''
    runHook preConfigure
    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"

    sed -i 's#/bin/pwd#pwd#g' src/Kernel/xnu/Makefile src/Userspace/busybox/upstream/Makefile

    sed -i "s#/usr/local/osxcross/bin/xcrun#${darwinCrossToolchain}/bin/xcrun#g" \
      src/Kernel/xnu/cmake/MakeInc.cmd.in tools/mig/mig.sh
    sed -i -E 's#(^|[[:space:]=])/(usr/)?bin/([A-Za-z_]+)#\1\3#g' \
      src/Kernel/xnu/cmake/MakeInc.cmd.in

    mkdir -p .nix-stubs
    cat > .nix-stubs/sw_vers <<'EOF'
#!/bin/sh
echo 11.3
EOF
    chmod +x .nix-stubs/sw_vers
    export PATH="$PWD/.nix-stubs:$PATH"
    sed -i '1s#.*#\#!'"$(command -v bash)"'#' src/Kernel/xnu/cmake/make_symbol_aliasing.sh.in
    patchShebangs src tools
    # patchShebangs does not rewrite this csh script, but /bin/csh is also
    # absent in the Nix sandbox.
    sed -i '1c#!${tcsh}/bin/tcsh -f' src/Kernel/xnu/SETUP/config/doconf

    export NIX_NATIVE_LD_PATH="${nativeLd}/bin/ld"
    export NIX_HOST_CC_PATH="${clang}/bin/clang"

    export NIX_MIGCOM_PATH="${nativeMigcom}/bin/migcom"
    export NIX_UNIFDEF_PATH="${nativeUnifdef}/bin/unifdef"

    cmake -S . -B build-nix -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/nix-toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Debug \
      -DOPENSSL_INCLUDE_DIR=${openssl.dev}/include \
      -DOPENSSL_CRYPTO_LIBRARY=${openssl.out}/lib/libcrypto.so \
      -DOPENSSL_SSL_LIBRARY=${openssl.out}/lib/libssl.so \
      -DZLIB_INCLUDE_DIR="$DARWIN_SDK_ROOT/usr/include" \
      -DZLIB_LIBRARY="$DARWIN_SDK_ROOT/usr/lib/libz.tbd" \
      -DZLIB_LIBRARY_RELEASE="$DARWIN_SDK_ROOT/usr/lib/libz.tbd" \
      -DLIBXML2_INCLUDE_DIR="$DARWIN_SDK_ROOT/usr/include/libxml2" \
      -DLIBXML2_LIBRARY="$DARWIN_SDK_ROOT/usr/lib/libxml2.tbd" \
      -DPUREDARWIN_MACOSX_SDK="$DARWIN_SDK_ROOT"
    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    mkdir -p .nix-stubs
    ln -sf "$PWD/tools/mig/mig.sh" .nix-stubs/mig
    ln -sf "${nativeMigcom}/bin/migcom" .nix-stubs/migcom
    ln -sf "${nativeUnifdef}/bin/unifdef" .nix-stubs/unifdef
    cat > .nix-stubs/libtool <<'EOF'
#!/bin/sh
if [ "$1" = "-static" ] && [ "$2" = "-o" ]; then
  out="$3"
  shift 3
  exec x86_64-apple-darwin20.4-ar rcs "$out" "$@"
fi
echo "unsupported libtool invocation: $*" >&2
exit 1
EOF
    chmod +x .nix-stubs/libtool
    export PATH="$PWD/.nix-stubs:$PWD/build-nix/tools/mig:$PWD/build-nix/tools/cctools/misc:$PWD/build-nix/tools/dtrace_ctf/tools:$PATH"
    ninja -C build-nix ${lib.escapeShellArgs buildTargets}
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out
  '' + lib.optionalString installUserland ''
    mkdir -p $out/bin
    cp build-nix/src/Userspace/helloapp/helloapp $out/bin/
    cp build-nix/src/Userspace/launchd/launchd $out/bin/
    cp build-nix/src/Userspace/busybox/build/busybox $out/bin/
  '' + lib.optionalString installKernel ''
    cp -R build-nix/src/Kernel/xnu/xnu/. $out/
  '' + lib.optionalString installXnuHeaders ''
    cp -R build-nix/src/Kernel/xnu/xnu_header_install/. $out/
  '' + lib.optionalString installKexts ''
    mkdir -p $out/System/Library/Extensions
    find build-nix/src/Kernel/Extensions -name '*.kext' -type d -prune \
      -exec cp -R '{}' $out/System/Library/Extensions/ ';'
  '' + lib.optionalString installBaseSystem ''
    cmake --install build-nix --component BaseSystem --prefix $out
  '' + ''
    runHook postInstall
  '';

  dontCheckForBrokenSymlinks = installKernel || installXnuHeaders || installBaseSystem;

  forceShare = lib.optionals (!installBaseSystem) [ "man" "doc" "info" ];
  dontMoveSbin = installBaseSystem;
  dontStrip = installBaseSystem;
  dontPatchELF = installBaseSystem;

  meta = with lib; {
    platforms = platforms.linux;
  };
}