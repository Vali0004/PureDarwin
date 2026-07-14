{ stdenv
, lib
, requireFile
, darwinCrossToolchain
, nativeLd
, libSystem
, libX11
, libxcb
, libXau
, libXdmcp
, xorgproto
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
  pname = "puredarwin-xeyes";
  version = "0.1";

  dontUnpack = true;

  buildPhase = ''
    runHook preBuild

    mkdir -p sdk build
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"

    cat > build/xeyes.c <<'EOF'
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void draw_eye(Display *dpy, Window win, GC gc, int cx, int cy, int px, int py)
{
    XSetForeground(dpy, gc, WhitePixel(dpy, DefaultScreen(dpy)));
    XFillArc(dpy, win, gc, cx - 55, cy - 40, 110, 80, 0, 360 * 64);
    XSetForeground(dpy, gc, BlackPixel(dpy, DefaultScreen(dpy)));
    XDrawArc(dpy, win, gc, cx - 55, cy - 40, 110, 80, 0, 360 * 64);

    int dx = px - cx;
    int dy = py - cy;
    if (dx > 26) dx = 26;
    if (dx < -26) dx = -26;
    if (dy > 16) dy = 16;
    if (dy < -16) dy = -16;
    XFillArc(dpy, win, gc, cx + dx - 12, cy + dy - 12, 24, 24, 0, 360 * 64);
}

int main(int argc, char **argv)
{
    const char *display_name = 0;
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], "-display") == 0) {
            display_name = argv[i + 1];
        }
    }

    Display *dpy = XOpenDisplay(display_name);
    if (!dpy) {
        return 1;
    }

    int screen = DefaultScreen(dpy);
    Window root = RootWindow(dpy, screen);
    unsigned long white = WhitePixel(dpy, screen);
    unsigned long black = BlackPixel(dpy, screen);
    Window win = XCreateSimpleWindow(dpy, root, 20, 20, 260, 140, 1, black, white);
    XStoreName(dpy, win, "xeyes");
    XSelectInput(dpy, win, ExposureMask | StructureNotifyMask);
    XMapWindow(dpy, win);
    GC gc = XCreateGC(dpy, win, 0, 0);

    for (;;) {
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == DestroyNotify) {
                return 0;
            }
        }

        Window rr, cr;
        int rx = 130, ry = 70, wx, wy;
        unsigned int mask;
        XQueryPointer(dpy, win, &rr, &cr, &rx, &ry, &wx, &wy, &mask);

        XClearWindow(dpy, win);
        draw_eye(dpy, win, gc, 80, 70, wx, wy);
        draw_eye(dpy, win, gc, 180, 70, wx, wy);
        XFlush(dpy);
        usleep(33000);
    }
}
EOF

    ${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-clang \
      -target x86_64-apple-macosx11.0 \
      -isysroot "$DARWIN_SDK_ROOT" \
      -I${libSystem}/usr/include \
      -I${lib.getDev libX11}/include \
      -I${lib.getDev libxcb}/include \
      -I${lib.getDev libXau}/include \
      -I${lib.getDev libXdmcp}/include \
      -I${lib.getDev xorgproto}/include \
      -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
      build/xeyes.c \
      -fuse-ld=${nativeLd}/bin/ld \
      -nostdlib \
      -L${libSystem}/usr/lib \
      -L${libX11}/lib \
      -L${libxcb}/lib \
      -L${libXau}/lib \
      -L${libXdmcp}/lib \
      -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib \
      -Wl,-dylinker_install_name,/usr/lib/dyld \
      -Wl,-platform_version,macos,11.0,11.5 \
      -Wl,-undefined,dynamic_lookup \
      -lX11 -lxcb -lXau -lXdmcp -lSystem \
      -o build/xeyes

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out/bin
    cp build/xeyes $out/bin/xeyes
    runHook postInstall
  '';

  dontFixup = true;

  meta = with lib; {
    platforms = platforms.linux;
  };
}
