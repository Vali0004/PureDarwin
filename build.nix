{ stdenv
, lib
, cmake
, ninja
, requireFile
, darwinCrossToolchain ? null
, nativeLd ? null
, nativeUnifdef ? null
, nativeMigcom ? null
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
, tinycc
, src ? ./.
, pname ? "puredarwin-nix-toolchain"
, buildTargets ? [ "helloapp" "launchd" ]
, enableProjects ? true
, enableKernel ? true
, enableLibraries ? true
, enableUserspace ? true
, enableTools ? true
, enableTcc ? false
, enableIOGraphicsFamily ? false
, installUserland ? true
, installKernel ? false
, installXnuHeaders ? false
, installKexts ? false
, installKextNames ? [ ]
, installLibSystem ? false
, installBaseSystem ? false
, prebuiltLibSystem ? null
, xnuKernelConfig ? "RELEASE"
, xorgDriverIncludes ? null
}:

let
  isDarwinHost = stdenv.hostPlatform.isDarwin;
  sdkTarball = if isDarwinHost then null else requireFile {
    name = "MacOSX11.3.sdk.tar.xz";
    sha256 = "9adc1373d3879e1973d28ad9f17c9051b02931674a3ec2a2498128989ece2cb1";
    message = ''
      MacOSX11.3.sdk.tar.xz (Apple SDK, proprietary - not fetchable/redistributable)
      is not yet in your Nix store. Register your local copy with:
        nix-store --add-fixed sha256 /path/to/MacOSX11.3.sdk.tar.xz
    '';
  };
  opensslCryptoLibrary =
    if isDarwinHost
    then "${openssl.out}/lib/libcrypto.dylib"
    else "${openssl.out}/lib/libcrypto.so";
  opensslSslLibrary =
    if isDarwinHost
    then "${openssl.out}/lib/libssl.dylib"
    else "${openssl.out}/lib/libssl.so";
in
stdenv.mkDerivation ({
  inherit pname;
  version = "0.1";

  inherit src;

  nativeBuildInputs = [
    cmake ninja bison flex perl bash ed unifdef tcsh
    pax coreutils findutils gawk gnused clang ruby iig
  ] ++ lib.optionals (!isDarwinHost) [
    darwinCrossToolchain nativeUnifdef nativeMigcom gnustep-base
  ];

  buildInputs = [ zlib openssl ] ++ lib.optionals (!isDarwinHost) [ libuuid ];

  configurePhase = ''
    runHook preConfigure
  '' + lib.optionalString (!isDarwinHost) ''
    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"
  '' + lib.optionalString isDarwinHost ''
    export DARWIN_SDK_ROOT="$(/usr/bin/xcrun --sdk macosx --show-sdk-path)"
  '' + ''

    if [ -e src/Kernel/xnu/Makefile ]; then
      sed -i 's#/bin/pwd#pwd#g' src/Kernel/xnu/Makefile
    fi
    if [ -e src/Userspace/busybox/upstream/Makefile ]; then
      sed -i 's#/bin/pwd#pwd#g' src/Userspace/busybox/upstream/Makefile
    fi

  '' + lib.optionalString (!isDarwinHost) ''
    if [ -e src/Kernel/xnu/cmake/MakeInc.cmd.in ]; then
      sed -i "s#/usr/local/osxcross/bin/xcrun#${darwinCrossToolchain}/bin/xcrun#g" \
        src/Kernel/xnu/cmake/MakeInc.cmd.in
      sed -i -E 's#(^|[[:space:]=])/(usr/)?bin/([A-Za-z_]+)#\1\3#g' \
        src/Kernel/xnu/cmake/MakeInc.cmd.in
    fi
    if [ -e tools/mig/mig.sh ]; then
      sed -i "s#/usr/local/osxcross/bin/xcrun#${darwinCrossToolchain}/bin/xcrun#g" \
        tools/mig/mig.sh
    fi
  '' + ''

    mkdir -p .nix-stubs
    cat > .nix-stubs/sw_vers <<'EOF'
#!/bin/sh
echo 11.3
EOF
    chmod +x .nix-stubs/sw_vers
    export PATH="$PWD/.nix-stubs:$PATH"
    if [ -e src/Kernel/xnu/cmake/make_symbol_aliasing.sh.in ]; then
      sed -i '1s#.*#\#!'"$(command -v bash)"'#' src/Kernel/xnu/cmake/make_symbol_aliasing.sh.in
    fi
    patchShebangs src ${lib.optionalString enableTools "tools"}
    if [ -e tools/mig ]; then
      patchShebangs tools/mig
    fi
    # patchShebangs does not rewrite this csh script, but /bin/csh is also
    # absent in the Nix sandbox.
    if [ -e src/Kernel/xnu/SETUP/config/doconf ]; then
      sed -i '1c#!${tcsh}/bin/tcsh -f' src/Kernel/xnu/SETUP/config/doconf
    fi

  '' + lib.optionalString (!isDarwinHost) ''
    export NIX_NATIVE_LD_PATH="${nativeLd}/bin/ld"
    export NIX_HOST_CC_PATH="${clang}/bin/clang"

    export NIX_MIGCOM_PATH="${nativeMigcom}/bin/migcom"
    export NIX_UNIFDEF_PATH="${nativeUnifdef}/bin/unifdef"
  '' + ''

    cmake -S . -B build-nix -G Ninja \
  '' + lib.optionalString (!isDarwinHost) ''
      -DCMAKE_TOOLCHAIN_FILE=cmake/nix-toolchain.cmake \
  '' + ''
      -DCMAKE_BUILD_TYPE=Debug \
      -DOPENSSL_INCLUDE_DIR=${openssl.dev}/include \
      -DOPENSSL_CRYPTO_LIBRARY=${opensslCryptoLibrary} \
      -DOPENSSL_SSL_LIBRARY=${opensslSslLibrary} \
      -DZLIB_INCLUDE_DIR="$DARWIN_SDK_ROOT/usr/include" \
      -DZLIB_LIBRARY="$DARWIN_SDK_ROOT/usr/lib/libz.tbd" \
      -DZLIB_LIBRARY_RELEASE="$DARWIN_SDK_ROOT/usr/lib/libz.tbd" \
      -DLIBXML2_INCLUDE_DIR="$DARWIN_SDK_ROOT/usr/include/libxml2" \
      -DLIBXML2_LIBRARY="$DARWIN_SDK_ROOT/usr/lib/libxml2.tbd" \
      -DPUREDARWIN_MACOSX_SDK="$DARWIN_SDK_ROOT" \
      -DPUREDARWIN_ENABLE_PROJECTS=${if enableProjects then "ON" else "OFF"} \
      -DPUREDARWIN_ENABLE_KERNEL=${if enableKernel then "ON" else "OFF"} \
      -DPUREDARWIN_ENABLE_LIBRARIES=${if enableLibraries then "ON" else "OFF"} \
      -DPUREDARWIN_ENABLE_USERSPACE=${if enableUserspace then "ON" else "OFF"} \
      -DPUREDARWIN_ENABLE_TOOLS=${if enableTools then "ON" else "OFF"} \
      -DPUREDARWIN_ENABLE_TCC=${if enableTcc then "ON" else "OFF"} \
      -DPUREDARWIN_ENABLE_IOGRAPHICS_FAMILY=${if enableIOGraphicsFamily then "ON" else "OFF"} \
      -DPUREDARWIN_XNU_KERNEL_CONFIG=${lib.escapeShellArg xnuKernelConfig} \
      -DPUREDARWIN_TCC_SOURCE=${tinycc.src} \
      ${lib.optionalString (xorgDriverIncludes != null) "-DPUREDARWIN_XORG_INCLUDE_DIRS=${lib.escapeShellArg (lib.concatStringsSep ";" xorgDriverIncludes)}"} \
      ${lib.optionalString (prebuiltLibSystem != null) "-DPUREDARWIN_PREBUILT_LIBSYSTEM_ROOT=${prebuiltLibSystem}"}
    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    mkdir -p .nix-stubs
  '' + lib.optionalString (!isDarwinHost) ''
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
  '' + lib.optionalString isDarwinHost ''
    export PATH="$PWD/.nix-stubs:$PATH"
  '' + ''
    ninja -C build-nix ${lib.escapeShellArgs buildTargets}
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out
  '' + lib.optionalString installUserland ''
    mkdir -p $out/bin $out/sbin
    for bin in \
      build-nix/src/Userspace/helloapp/helloapp \
      build-nix/src/Userspace/busybox/build/busybox \
      build-nix/src/Userspace/sw_vers/sw_vers \
      build-nix/src/Userspace/ps/ps \
      build-nix/src/Userspace/system_cmds/mkfile \
      build-nix/src/Userspace/system_cmds/sync \
      build-nix/src/Userspace/tcc/build/tcc \
      build-nix/src/Userspace/fbtri/fbtri \
      build-nix/src/Userspace/malloctest/malloctest \
      build-nix/src/Userspace/sockettest/sockettest \
      build-nix/src/Userspace/iokittest/iokittest \
      build-nix/src/Userspace/iokittest/ioreg
    do
      if [ -x "$bin" ]; then
        cp "$bin" $out/bin/
      fi
    done
    if [ -x build-nix/src/Userspace/launchd/launchd ]; then
      cp build-nix/src/Userspace/launchd/launchd $out/sbin/
    fi
    _pdgop_drv="$(find build-nix -name 'puredarwingop_drv.dylib' -print -quit 2>/dev/null || true)"
    echo "puredarwingop driver module: ''${_pdgop_drv:-<not built>}"
    if [ -n "$_pdgop_drv" ]; then
      mkdir -p $out/usr/lib/xorg/modules/drivers
      cp "$_pdgop_drv" $out/usr/lib/xorg/modules/drivers/puredarwingop_drv.so
    fi
  '' + lib.optionalString installKernel ''
    cp -R build-nix/src/Kernel/xnu/xnu/. $out/
  '' + lib.optionalString installXnuHeaders ''
    cp -R build-nix/src/Kernel/xnu/xnu_header_install/. $out/
  '' + lib.optionalString installKexts ''
    mkdir -p $out/System/Library/Extensions
  '' + lib.optionalString (installKexts && installKextNames == [ ]) ''
    find build-nix/src/Kernel/Extensions -name '*.kext' -type d -prune \
      -exec cp -R '{}' $out/System/Library/Extensions/ ';'
  '' + lib.optionalString (installKexts && installKextNames != [ ]) ''
    for kext in ${lib.escapeShellArgs installKextNames}; do
      kext_path="$(find build-nix/src/Kernel/Extensions -name "$kext" -type d -print -quit)"
      if [ -z "$kext_path" ]; then
        echo "missing kext bundle: $kext" >&2
        exit 1
      fi
      cp -R "$kext_path" "$out/System/Library/Extensions/"
    done
  '' + lib.optionalString installLibSystem ''
    mkdir -p $out/usr/lib/system
    cp build-nix/src/Libraries/libSystem/stub/libSystem.B.dylib $out/usr/lib/
    cp -P build-nix/src/Libraries/libSystem/stub/libSystem.dylib $out/usr/lib/
    cp build-nix/src/Libraries/libSystem/libdyld/libdyld.dylib $out/usr/lib/system/
    if [ -e build-nix/src/Libraries/libSystem/libsystem_kernel/libsystem_kernel.a ]; then
      cp build-nix/src/Libraries/libSystem/libsystem_kernel/libsystem_kernel.a $out/usr/lib/system/
    fi
    if [ -e build-nix/src/Libraries/libSystem/libsystem_kernel/syscalls.a ]; then
      cp build-nix/src/Libraries/libSystem/libsystem_kernel/syscalls.a $out/usr/lib/system/
    fi
    cp build-nix/src/Libraries/dyld/dyld $out/usr/lib/
  '' + lib.optionalString installBaseSystem ''
    cmake --install build-nix --component BaseSystem --prefix $out
  '' + ''
    runHook postInstall
  '';

  dontCheckForBrokenSymlinks = installKernel || installXnuHeaders || installBaseSystem;

  forceShare = lib.optionals (!installBaseSystem) [ "man" "doc" "info" ];
  dontMoveSbin = installBaseSystem || installUserland;
  dontStrip = installBaseSystem;
  dontPatchELF = installBaseSystem;

  meta = with lib; {
    platforms = platforms.linux ++ platforms.darwin;
  };
} // lib.optionalAttrs (!isDarwinHost) {
  NIX_DARWIN_TOOLCHAIN_DIR = "${darwinCrossToolchain}/bin";
})
