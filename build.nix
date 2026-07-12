{ stdenv
, lib
, cmake
, ninja
, requireFile
, darwinCrossToolchain
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
  pname = "puredarwin-nix-toolchain-smoke";
  version = "0.1";

  src = ./.;

  # darwinCrossToolchain must be on PATH, not just referenced via
  # NIX_DARWIN_TOOLCHAIN_DIR below: some CMakeLists (e.g. src/Kernel/xnu)
  # find_program() a bare "dsymutil" rather than using CMAKE_* variables.
  # bison/flex: tools/mig (Mach Interface Generator, a host tool needed to
  # build libSystem's MIG-generated IPC stubs, which launchd links against)
  # invokes CMake's plain "yacc"/"lex" find_program targets under the hood.
  # perl: src/Libraries/libSystem/libc's generate_features.pl build step.
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
  nativeBuildInputs = [
    cmake ninja darwinCrossToolchain bison flex perl bash ed unifdef tcsh
    gnustep-base pax coreutils findutils gawk gnused clang
  ];

  # tools/dtrace_ctf's ctfconvert is a HOST tool (runs on the build machine,
  # not cross-compiled) and needs a real libz.so to link against - the
  # SDK's libz.tbd (used above only to satisfy FindZLIB during the
  # cross-compiled side's configure) is a Mach-O stub, useless here.
  # libuuid: xnu's SETUP/kextsymboltool (a real host-native build tool,
  # see clang comment above) #includes <uuid/uuid.h>.
  buildInputs = [ zlib libuuid ];

  NIX_DARWIN_TOOLCHAIN_DIR = "${darwinCrossToolchain}/bin";

  configurePhase = ''
    runHook preConfigure
    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"
    # xnu's own Makefile hardcodes /bin/pwd (doesn't exist in the sandbox -
    # only /bin/sh is provided); a real Unix filesystem always has it, so
    # this is XNU's assumption, not something worth carrying upstream.
    sed -i 's#/bin/pwd#pwd#g' src/Kernel/xnu/Makefile
    # xnu/cmake/MakeInc.cmd.in hardcodes XCRUN = /usr/local/osxcross/bin/xcrun
    # (the non-Darwin-host branch) and routes essentially every build tool
    # (CC, MIG, STRIP, NM, UNIFDEF, DSYMUTIL, ...) through
    # `$(XCRUN) -sdk $(SDKROOT) -find <tool>` - our own xcrun shim already
    # implements that exact `-find` dispatch, so just repoint it.
    sed -i "s#/usr/local/osxcross/bin/xcrun#${darwinCrossToolchain}/bin/xcrun#g" \
      src/Kernel/xnu/cmake/MakeInc.cmd.in tools/mig/mig.sh
    # Same file also hardcodes a whole slate of /bin/*, /usr/bin/* tool
    # paths (cp, mv, mkdir, tr, sed, awk, find, xargs, pax, plutil, ...) -
    # standard coreutils/findutils/gawk/gnused/pax names, just not at
    # those literal absolute paths in the sandbox; strip the prefix so
    # plain PATH lookup resolves them (all present via nativeBuildInputs).
    sed -i -E 's#(^|[[:space:]=])/(usr/)?bin/([A-Za-z_]+)#\1\3#g' \
      src/Kernel/xnu/cmake/MakeInc.cmd.in
    # sw_vers is a real macOS-only tool with no Linux equivalent; xnu only
    # wants a version string (HOST_OS_VERSION) out of it, so stub it.
    mkdir -p .nix-stubs
    cat > .nix-stubs/sw_vers <<'EOF'
#!/bin/sh
echo 11.3
EOF
    chmod +x .nix-stubs/sw_vers
    export PATH="$PWD/.nix-stubs:$PATH"
    sed -i '1s#.*#\#!'"$(command -v bash)"'#' src/Kernel/xnu/cmake/make_symbol_aliasing.sh.in
    # src/Libraries/libcxxabi's CMakeLists doesn't read DARWIN_SDK_ROOT -
    # it derives its own -isysroot from $ENV{OSXCROSS_SDK} or, failing
    # that, this cache variable (which top-level CMakeLists.txt otherwise
    # defaults to the real host's /usr/local/osxcross/SDK path).
    #
    # patchShebangs: many build scripts across the tree (libc's headers.sh,
    # xnu's san/tools/validate_blacklist.sh, AvailabilityVersions/
    # availability.pl, ...) have a literal #!/bin/bash or #!/usr/bin/perl
    # shebang, none of which exist in the sandbox (nixpkgs bash/perl live
    # in the store); rewrite them all up front rather than one at a time.
    patchShebangs src tools
    # tools/cctools/ld64 (a target we don't build here, but CMake still
    # configures the whole tree) find_package(OpenSSL REQUIRED)s a real
    # libcrypto; osxcross satisfies this from a bundled macports package,
    # not the bare Apple SDK. We don't build or link that target in this
    # smoke test, so any OpenSSL that lets configure succeed is fine here.
    # CMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY (set for the SDK-restricted
    # search everything else needs) means even an OPENSSL_ROOT_DIR hint
    # still gets filtered out by find_library, so bypass the search
    # entirely with the exact final cache variables FindOpenSSL checks.
    # Same story for ZLIB (tools/dtrace_ctf): the SDK has zlib.h/zconf.h but
    # only versioned/bare .tbd stubs for the library, and CMake's FindZLIB
    # find_library() doesn't treat .tbd as a valid suffix by default - point
    # it straight at the SDK's stub rather than teaching CMake a new suffix.
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
    # MakeInc.cmd.in's xcrun-based fallback (see above) resolves MIG/
    # MIGCOM/LIBTOOL/UNIFDEF/etc by bare name via PATH, but they're the
    # project's own build products (or, for mig, a checked-in wrapper
    # script named mig.sh, not "mig") living under source/build-nix
    # subdirectories, never installed anywhere PATH-visible. ninja builds
    # them (migcom etc.) before xnu_headers.extproj's custom command
    # actually runs, since that's a real dependency in the CMake graph -
    # only the directories need to be reachable, not built up-front here.
    mkdir -p .nix-stubs
    ln -sf "$PWD/tools/mig/mig.sh" .nix-stubs/mig
    export PATH="$PWD/.nix-stubs:$PWD/build-nix/tools/mig:$PWD/build-nix/tools/cctools/misc:$PWD/build-nix/tools/unifdef:$PWD/build-nix/tools/dtrace_ctf/tools:$PATH"
    ninja -C build-nix helloapp launchd
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/bin
    cp build-nix/src/Userspace/helloapp/helloapp $out/bin/
    cp build-nix/src/Userspace/launchd/launchd $out/bin/
    runHook postInstall
  '';

  meta = with lib; {
    description = "Smoke test: PureDarwin userspace targets built via nixpkgs LLVM instead of osxcross";
    platforms = platforms.linux;
  };
}
