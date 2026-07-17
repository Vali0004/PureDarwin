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
  pname = "puredarwin-fastfetch";
  inherit (fastfetch) version;
  src = fastfetch.src;

  nativeBuildInputs = [ cmake ninja pkg-config ];

  cmakeFlags = [
    "-DCMAKE_TOOLCHAIN_FILE=${../../cmake/nix-toolchain.cmake}"
    "-DNIX_DARWIN_TOOLCHAIN_DIR=${darwinCrossToolchain}/bin"
    "-DHAVE_MALLOC_USABLE_SIZE=OFF"
    "-DHAVE_PIPE2=OFF"
    # memrchr doesn't exist on real macOS either - same false-positive risk.
    "-DHAVE_MEMRCHR=OFF"
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
    # PureDarwin ships none of macOS's media/UI frameworks (AVFoundation,
    # Cocoa, CoreMedia, ...) - fastfetch's upstream CMakeLists direct-links
    # them unconditionally on APPLE. A first attempt weak-linked them
    # (-weak_framework), but Cocoa is an umbrella framework whose own .tbd
    # re-exports AppKit/Foundation, and that re-export chain still pulls
    # AppKit in as a hard, non-weak LC_LOAD_DYLIB regardless of how Cocoa
    # itself was linked - so weak-linking only the top-level name doesn't
    # help. None of these frameworks will ever exist here, so don't emit
    # load commands for them at all; -undefined,dynamic_lookup (passed
    # below) already defers *symbol* resolution generically, which is all
    # any of fastfetch's Apple-only detection modules actually need.
    # Every *_apple.m/.c detection module that talks to a real macOS UI/media
    # framework (AVFoundation, CoreWLAN, IOBluetooth, AppKit, CoreText,
    # MediaRemote, ...) references Objective-C classes and string constants
    # directly - those are eager (non-lazy) symbols, so -undefined,dynamic_lookup
    # doesn't save us the way it does for plain function calls. Rather than
    # fix these one crash at a time, swap each one for the *_nosupport.c
    # stub fastfetch already ships for platforms without that feature - CPU/
    # memory/OS/board/etc. modules stay on their real *_apple.c/.m sources
    # since those only use IOKit/sysctl, which do work here.
    # theme_apple.c is plain C (CFSTR only, no @"..."/NSArray/NSString) -
    # it was swapped by an earlier heuristic pass before actually checking
    # its content; now that PureDarwin has a real CoreFoundation (see
    # corefoundation.nix), it can use its real implementation like the other
    # CFSTR-only C modules (cpu/battery/keyboard/mouse/etc.) already do.
    for base in camera wifi bluetooth bluetoothradio cursor font media wallpaper wm wmtheme physicalmemory; do
      sed -i "s#src/detection/$base/''${base}_apple\.[mc]#src/detection/$base/''${base}_nosupport.c#" CMakeLists.txt
    done

    # terminalfont_apple.m has no upstream _nosupport.c fallback, and it's
    # the last caller of common/apple/osascript.m (NSAppleScript) now that
    # wallpaper is stubbed - write a minimal no-op replacement matching its
    # one exported signature so nothing pulls NSAppleScript in at all.
    cat > src/detection/terminalfont/terminalfont_nosupport.c <<'TFEOF'
#include "terminalfont.h"
#include "detection/terminalshell/terminalshell.h"

bool ffDetectTerminalFontPlatform(const FFTerminalResult* terminal, FFTerminalFontResult* terminalFont)
{
    (void) terminal;
    (void) terminalFont;
    return false;
}
TFEOF
    sed -i 's#src/detection/terminalfont/terminalfont_apple\.m#src/detection/terminalfont/terminalfont_nosupport.c#' CMakeLists.txt
    sed -i '/src\/common\/apple\/osascript\.m/d' CMakeLists.txt

    # common/apple/version.m (unconditionally compiled) reads a .app bundle's
    # Info.plist via NSDictionary/Foundation to get its display name/version.
    # Its only remaining caller (now that wm_apple.m is stubbed) is
    # terminalshell.c, which already handles a false return by just skipping
    # the enrichment - replace it with a plain stub instead of pulling in
    # Foundation for a single optional nicety.
    cat > src/common/apple/version.m <<'VEREOF'
#include "common/apple/version.h"

bool ffGetAppNameAndVersion(const char* exePath, FFstrbuf* retName, FFstrbuf* retVersion)
{
    (void) exePath;
    (void) retName;
    (void) retVersion;
    return false;
}
VEREOF

    # os_apple.m uses NSProcessInfo/Foundation for OS name+version and has no
    # upstream _nosupport.c - unlike the peripheral modules above, OS
    # identification is core to what fastfetch is for, so don't just stub it
    # to nothing. FFPlatform_unix.c (already compiled unconditionally here)
    # populates instance.state.platform.sysinfo via uname() for every
    # platform - reuse that instead of Foundation, same pattern os_obsd.c
    # already uses for OpenBSD.
    cat > src/detection/os/os_nosupport.c <<'OSEOF'
#include "os.h"

void ffDetectOSImpl(FFOSResult* os)
{
    ffStrbufSetStatic(&os->name, "PureDarwin");
    ffStrbufSetStatic(&os->prettyName, "PureDarwin");
    ffStrbufSet(&os->version, &instance.state.platform.sysinfo.release);
    ffStrbufSet(&os->versionID, &instance.state.platform.sysinfo.release);
    ffStrbufSetStatic(&os->id, "puredarwin");
}
OSEOF
    sed -i 's#src/detection/os/os_apple\.m#src/detection/os/os_nosupport.c#' CMakeLists.txt

    # gpu_apple.c (208 lines, IOKit-based) is the real GPU detection and
    # already defines ffDetectGPUImpl itself - gpu_apple.m is just two small
    # helpers it calls (Metal.framework device enumeration, KextManager
    # driver-version lookup) that pull in NSArray/NSDictionary. Swapping the
    # whole thing to gpu_nosupport.c would collide (duplicate ffDetectGPUImpl)
    # and throw away the real detection - stub just the two helpers instead.
    cat > src/detection/gpu/gpu_apple.m <<'GPUEOF'
#include "gpu.h"

const char* ffGpuDetectDriverVersion(FFlist* gpus)
{
    (void) gpus;
    return "Driver version detection not supported";
}

const char* ffGpuDetectMetal(FFlist* gpus)
{
    (void) gpus;
    return "Metal API is not supported here";
}
GPUEOF

    awk '
      /-framework AVFoundation/ { start=NR-2 }
      { lines[NR]=$0 }
      /^    \)$/ { if (start && !stop) stop=NR }
      END {
        for (i=1; i<=NR; i++) {
          if (start && i>=start && i<=stop) continue
          print lines[i]
        }
      }
    ' CMakeLists.txt > CMakeLists.txt.new
    mv CMakeLists.txt.new CMakeLists.txt

    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"
    export PATH="${darwinCrossToolchain}/bin:$PATH"
    export NIX_DARWIN_TOOLCHAIN_DIR="${darwinCrossToolchain}/bin"

    # Same stub-dylib/-nostdlib pattern as every other cross-built port:
    # link against our real static libSystem.exports archive, not the
    # SDK's stub dylibs.
    # This project's native ld doesn't do ld64's automatic -syslibroot-based
    # framework search the way real ld64 does with just -isysroot; pass the
    # SDK's Frameworks dir explicitly so "-framework AVFoundation" etc.
    # resolve against the SDK's .tbd stubs (present for all the frameworks
    # fastfetch links; real symbol resolution still comes from
    # -undefined,dynamic_lookup since none of these frameworks have a real
    # implementation in PD).
    # CoreFoundation (corefoundation.nix, cross-built from PureDarwin's own
    # src/Libraries/CoreFoundation) provides the real CFSTR/CFString/
    # CFDictionary/__CFConstantStringClassReference machinery the plain-C
    # *_apple.c IOKit-detection modules (cpu, battery, keyboard, mouse,
    # diskio, poweradapter, bootmgr, brightness, physicaldisk, gamepad, dns,
    # displayserver, gpu, theme) need. Linked as a real dylib now (matching
    # real Darwin) rather than -force_load'd as a static archive:
    # CFUniChar.c's __CFGetSectDataPtr looks for its own image by comparing
    # against &_mh_dylib_header, a magic symbol the linker only ever
    # synthesizes for an actual MH_DYLIB - statically merging CF into the
    # executable meant that symbol never existed, breaking the link.
    export LDFLAGS="-isysroot $DARWIN_SDK_ROOT -F$DARWIN_SDK_ROOT/System/Library/Frameworks -fuse-ld=${nativeLd}/bin/ld -nostdlib -Wl,-Z -L${libSystem}/usr/lib -L${corefoundation}/usr/lib -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib -Wl,-dylinker_install_name,/usr/lib/dyld -Wl,-platform_version,macos,11.0,11.5 -Wl,-undefined,dynamic_lookup -lCoreFoundation -lSystem"
    export CFLAGS="-isysroot $DARWIN_SDK_ROOT -I${libSystem}/usr/include -I${corefoundation}/include -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0"
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
