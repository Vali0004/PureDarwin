{ stdenv
, lib
, requireFile
, cmake
, ninja
, darwinCrossToolchain
, nativeLd
, llvmPackages
, libSystem
, icu
, src
, pdCompatInclude
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

  nativeBuildInputs = [ cmake ninja llvmPackages.bintools ];

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

    # CF's own CMakeLists links against "BlocksRuntime"/"dispatch" as plain
    # CMake target names - real, in-tree targets in the full monorepo build,
    # but this is a standalone CF configure with no such targets defined, so
    # CMake degrades them to bare -lBlocksRuntime/-ldispatch. Everything
    # those would provide (Blocks runtime globals, dispatch functions) is
    # already in libSystem.B.dylib itself; ld64.lld still hard-errors
    # "library not found" on a bare -lname with nothing on the search path
    # to satisfy it, so give it empty placeholder archives instead of
    # patching the upstream CMakeLists to remove them.
    # A genuinely empty archive ("!<arch>\n" and nothing else) is accepted by
    # lld but real ld64 rejects it ("file too small") - give each one a
    # single harmless local-symbol object file instead of no members at all.
    mkdir -p placeholder-libs
    echo 'static int puredarwin_placeholder;' > placeholder-libs/placeholder.c
    ${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-clang -isysroot "$DARWIN_SDK_ROOT" -c placeholder-libs/placeholder.c -o placeholder-libs/placeholder.o
    ${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-ar crs placeholder-libs/libBlocksRuntime.a placeholder-libs/placeholder.o
    ${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-ar crs placeholder-libs/libdispatch.a placeholder-libs/placeholder.o

    cmake -S . -B build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=${../../cmake/nix-toolchain.cmake} \
      -DNIX_DARWIN_TOOLCHAIN_DIR=${darwinCrossToolchain}/bin \
      -DNIX_DARWIN_SDK_ROOT=$DARWIN_SDK_ROOT \
      -DBUILD_SHARED_LIBS=ON \
      -DCMAKE_C_FLAGS="-isysroot $DARWIN_SDK_ROOT -I${libSystem}/usr/include -I${pdCompatInclude} -I${lib.getDev icu}/include -DU_DISABLE_RENAMING=1 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0" \
      -DCMAKE_SHARED_LINKER_FLAGS="-fuse-ld=${nativeLd}/bin/ld -nostdlib -L$PWD/placeholder-libs -L${libSystem}/usr/lib -lSystem -Wl,-install_name,/usr/lib/libCoreFoundation.dylib -Wl,-platform_version,macos,11.0,11.5"

    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    # CMake computes this target's LINK_FLAGS ninja variable ending in a
    # dangling, unpaired "-Xlinker" token (own internal quirk when
    # cross-compiling a SHARED library for Darwin without CMAKE_HOST_APPLE -
    # not caused by anything in our own CMAKE_SHARED_LINKER_FLAGS, confirmed
    # by inspecting the generated build.ninja directly). That stray token
    # then swallows the rule's own following "-o $TARGET_FILE", so clang
    # sees "-Xlinker -o <path>" and treats the bare path as a normal input
    # file (which doesn't exist, since it's the *output*) instead of an
    # output path. It has no argument of its own, so it's safe to just
    # delete it from wherever CMake wrote the assignment.
    find build -name '*.ninja' -exec \
      sed -i -E 's/[[:space:]]-Xlinker$//' {} +
    ninja -C build
    runHook postBuild
  '';

  # Real x86_64-apple-darwin dylib now (not statically merged into every
  # consumer): CFUniChar.c's __CFGetSectDataPtr walks _dyld_image_count()
  # looking for the image whose header equals &_mh_dylib_header - that
  # magic symbol is only ever synthesized by the linker for an actual
  # MH_DYLIB image. Building CF as a static archive and -force_load'ing it
  # into an executable meant that symbol never existed at all (the
  # executable gets _mh_execute_header instead), which is what broke
  # fastfetch's link. A real dylib gets it for free.
  installPhase = ''
    runHook preInstall
    mkdir -p $out/usr/lib $out/include
    cp build/CoreFoundation.framework/libCoreFoundation.dylib $out/usr/lib/
    cp -a build/CoreFoundation.framework/Headers/. $out/include/
    cp -a build/CoreFoundation.framework/PrivateHeaders/. $out/include/
    runHook postInstall
  '';

  dontFixup = true;

  meta = with lib; {
    description = "PureDarwin CoreFoundation (from apple/swift-corelibs-foundation), cross-built as a real dylib";
    platforms = platforms.linux;
  };
}
