{ stdenv
, lib
, requireFile
, darwinCrossToolchain
, nativeLd
, libSystem
, fastfetch
, cmake
, ninja
, pkg-config
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
  pname = "puredarwin-fastfetch";
  inherit (fastfetch) version;
  src = fastfetch.src;

  nativeBuildInputs = [ cmake ninja pkg-config ];

  # Every optional backend (Vulkan, sqlite3, ImageMagick, D-Bus, Wayland,
  # X11, DRM/GPU stuff, PulseAudio, ...) needs a real library we don't
  # have; explicitly turn every one off rather than let CMake's own
  # find_package probes discover host copies through the SDK's stub .tbds
  # (the same "SDK stub dylib trap" that bit toybox/curl - see those
  # derivations' comments). What's left is fastfetch's built-in
  # POSIX/sysctl-based detection, which is most of what matters on a
  # system with no display server, GPU driver, or package manager anyway.
  cmakeFlags = [
    "-DCMAKE_TOOLCHAIN_FILE=${../../cmake/nix-toolchain.cmake}"
    "-DNIX_DARWIN_TOOLCHAIN_DIR=${darwinCrossToolchain}/bin"
    "-DBUILD_FLASHFETCH=OFF"
    "-DBUILD_TESTS=OFF"
    "-DENABLE_SYSTEM_YYJSON=OFF"
    "-DENABLE_SYSTEM_JSONC=OFF"
    "-DENABLE_DIRECTX_HEADERS=OFF"
    "-DENABLE_IMAGEMAGICK6=OFF"
    "-DENABLE_IMAGEMAGICK7=OFF"
    "-DENABLE_CHAFA=OFF"
    "-DENABLE_SQLITE3=OFF"
    "-DENABLE_LIBZFS=OFF"
    "-DENABLE_PULSE=OFF"
    "-DENABLE_VA=OFF"
    "-DENABLE_VDPAU=OFF"
    "-DENABLE_DDCUTIL=OFF"
    "-DENABLE_DBUS=OFF"
    "-DENABLE_EET=OFF"
    "-DENABLE_ELF=OFF"
    "-DENABLE_GIO=OFF"
    "-DENABLE_DCONF=OFF"
    "-DENABLE_ZLIB=OFF"
    "-DENABLE_OPENCL=OFF"
    "-DENABLE_EGL=OFF"
    "-DENABLE_GLX=OFF"
    "-DENABLE_RPM=OFF"
    "-DENABLE_DRM=OFF"
    "-DENABLE_DRM_AMDGPU=OFF"
    "-DENABLE_VULKAN=OFF"
    "-DENABLE_WAYLAND=OFF"
    "-DENABLE_XCB_RANDR=OFF"
    "-DENABLE_XRANDR=OFF"
    "-DENABLE_XFCONF=OFF"
    "-DENABLE_X11=OFF"
    "-DENABLE_LIBCJSON=OFF"
    "-DENABLE_THREADS=OFF"
  ];

  preConfigure = ''
    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"
    export PATH="${darwinCrossToolchain}/bin:$PATH"

    # Same stub-dylib/-nostdlib pattern as every other cross-built port:
    # link against our real static libSystem.exports archive, not the
    # SDK's stub dylibs.
    export LDFLAGS="-isysroot $DARWIN_SDK_ROOT -fuse-ld=${nativeLd}/bin/ld -nostdlib -Wl,-Z -L${libSystem}/usr/lib -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib -Wl,-dylinker_install_name,/usr/lib/dyld -Wl,-platform_version,macos,11.0,11.5 -Wl,-undefined,dynamic_lookup -lSystem"
    export CFLAGS="-isysroot $DARWIN_SDK_ROOT -I${libSystem}/usr/include -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0"
  '';

  dontFixup = true;
  dontStrip = true;

  installPhase = ''
    runHook preInstall
    mkdir -p $out/bin
    cp fastfetch $out/bin/fastfetch 2>/dev/null || find . -maxdepth 2 -name fastfetch -type f -exec cp {} $out/bin/fastfetch \;
    runHook postInstall
  '';

  meta = with lib; {
    platforms = platforms.linux;
  };
}
