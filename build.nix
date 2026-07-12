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
# Run the project's real `cmake --install --component BaseSystem` into $out
# instead of (or in addition to) the cherry-picked copies above - this is
# the full build-install layout userland-test/stage.sh and
# kc-tools/build-kc.sh consume: bin/, sbin/, usr/lib/{dyld,libSystem.B.dylib,
# system/libdyld.dylib}, System/Library/Extensions/*.kext and
# System/Library/Kernels/.
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

  # darwinCrossToolchain must be on PATH, not just referenced via
  # NIX_DARWIN_TOOLCHAIN_DIR below: some CMakeLists (e.g. src/Kernel/xnu)
  # find_program() a bare "dsymutil" rather than using CMAKE_* variables.
  # bison/flex: tools/mig (Mach Interface Generator, a host tool needed to
  # build libSystem's MIG-generated IPC stubs, which launchd links against)
  # invokes CMake's plain "yacc"/"lex" find_program targets under the hood.
  # perl: src/Libraries/libSystem/libc's generate_features.pl build step.
  # ruby: src/Libraries/dyld's VersionMap.h / for_dyld_priv.inc generator.
  # ed: libc's headers.sh strips private-header content via classic `ed`.
  # unifdef: same script also runs a HOST unifdef over headers - the one
  # PureDarwin builds itself (tools/unifdef) is cross-compiled Mach-O, so
  # it can't run here either; use nixpkgs' native ELF build instead.
  # tcsh: some xnu build scripts are tcsh, not sh/bash.
  # gnustep-base: provides a usable `plutil` (host tool, plist munging).
  # pax/coreutils/findutils/gawk/gnused: xnu/cmake/MakeInc.cmd.in hardcodes
  # a whole slate of /bin/* and /usr/bin/* tool paths (cp, mv, mkdir, tr,
  # sed, awk, find, xargs, pax, ...) that only exist at those absolute
  # paths on a real Unix filesystem; stripped down to bare names (see
  # sed below) so plain PATH lookup finds these nixpkgs equivalents.
  # clang (plain, native): xnu's SETUP/* tools (setsegname, kextsymboltool,
  # decomment, installfile, replacecontents) are genuine host-native build
  # tools - they compile and RUN on the build machine during the build
  # (a real Canadian-cross case, unlike everything else here which is
  # cross-compiled Mach-O), and their Makefiles invoke bare "clang".
  # iig: IOKit Interface Generator, used by DriverKit/IOKit targets.
  nativeBuildInputs = [
    cmake ninja darwinCrossToolchain bison flex perl bash ed unifdef tcsh
    gnustep-base pax coreutils findutils gawk gnused clang ruby iig
  ];

  # tools/dtrace_ctf's ctfconvert is a HOST tool (runs on the build machine,
  # not cross-compiled) and needs a real libz.so to link against - the
  # SDK's libz.tbd (used above only to satisfy FindZLIB during the
  # cross-compiled side's configure) is a Mach-O stub, useless here.
  # libuuid: xnu's SETUP/kextsymboltool (a real host-native build tool,
  # see clang comment above) #includes <uuid/uuid.h>.
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

    # Real cctools ld64 (host_ld target, tools/cctools/ld64) is what
    # actually understands xnu's "-kernel -static" kernel-linking mode and
    # emits a correct entry-point load command - nixpkgs' ld64.lld (used
    # for every other cross-compiled target) doesn't implement either, and
    # a kernel linked with it boots into "failed to find entry vmaddr".
    # Built as its own Nix derivation (native-ld.nix) instead of inline
    # here so unrelated source edits (xnu, userland, ...) don't force an
    # ~8 minute rebuild of it on every iteration.
    export NIX_NATIVE_LD_PATH="${nativeLd}/bin/ld"
    export NIX_HOST_CC_PATH="${clang}/bin/clang"
    # Same reasoning, same pattern - migcom.nix/unifdef.nix instead of
    # in-tree native builds of these two, see tools/mig/CMakeLists.txt
    # and tools/unifdef/CMakeLists.txt for the consuming side.
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
  '' + lib.optionalString installUserland ''
    mkdir -p $out/bin
    cp build-nix/src/Userspace/helloapp/helloapp $out/bin/
    cp build-nix/src/Userspace/launchd/launchd $out/bin/
    cp build-nix/src/Userspace/busybox/build/busybox $out/bin/
  '' + lib.optionalString installKernel ''
    mkdir -p $out
    cp -R build-nix/src/Kernel/xnu/xnu/. $out/
  '' + lib.optionalString installBaseSystem ''
    cmake --install build-nix --component BaseSystem --prefix $out
  '' + ''
    runHook postInstall
  '';

  # xnu's own install legitimately produces a dangling
  # System.framework/Resources -> Versions/Current/Resources symlink
  # (Current -> A, but A/Resources is never populated for this bare
  # framework skeleton) - real Darwin ships the same layout; only Nix's
  # fixupPhase objects.
  dontCheckForBrokenSymlinks = installKernel || installBaseSystem;

  # For the BaseSystem tree, keep Nix's fixups entirely away from the
  # output: it force-moves sbin/ into bin/ (stage.sh stages /sbin/launchd
  # from sbin/), runs host llvm-install-name-tool/strip over Mach-O
  # binaries ("unsupported load command"), and none of its ELF-oriented
  # rewrites apply to a Darwin rootfs anyway.
  forceShare = lib.optionals (!installBaseSystem) [ "man" "doc" "info" ];
  dontMoveSbin = installBaseSystem;
  dontStrip = installBaseSystem;
  dontPatchELF = installBaseSystem;

  meta = with lib; {
    description = "Smoke test: PureDarwin userspace targets built via nixpkgs LLVM instead of osxcross";
    platforms = platforms.linux;
  };
}
