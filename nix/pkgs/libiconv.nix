{ stdenv
, lib
, requireFile
, gnumake
, darwinCrossToolchain
, nativeLd
, libSystem
, libiconvReal
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
  pname = "puredarwin-libiconv";
  inherit (libiconvReal) version src;

  nativeBuildInputs = [
    gnumake
  ];

  postPatch = ''
    # The Darwin SDK's MB_CUR_MAX_L macro references the private
    # ____mb_cur_max_l symbol, which PureDarwin libSystem does not export.
    sed -i 's/strcmp (codeset, "UTF-8") == 0 && MB_CUR_MAX_L (uselocale (NULL)) <= 1/0/' libcharset/lib/localcharset.c

    # GNU libiconv exports libiconv_* on Darwin by default, while some
    # configure paths still link consumers against the POSIX iconv_* names.
    # Provide both spellings from the dylib.
    cat >> lib/iconv.c <<'EOF'

#undef iconv_open
#undef iconv
#undef iconv_close

__attribute__((visibility("default")))
iconv_t
iconv_open(const char *tocode, const char *fromcode)
{
  return libiconv_open(tocode, fromcode);
}

__attribute__((visibility("default")))
size_t
iconv(iconv_t cd, ICONV_CONST char **inbuf, size_t *inbytesleft,
      char **outbuf, size_t *outbytesleft)
{
  return libiconv(cd, inbuf, inbytesleft, outbuf, outbytesleft);
}

__attribute__((visibility("default")))
int
iconv_close(iconv_t cd)
{
  return libiconv_close(cd);
}
EOF
  '';

  configurePhase = ''
    runHook preConfigure

    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"
    export PATH="${darwinCrossToolchain}/bin:$PATH"
    export CC="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-clang"
    export LD="${nativeLd}/bin/ld"
    export AR="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-ar"
    export RANLIB="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-ranlib"
    export STRIP="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-strip"
    export NM="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-nm"
    export OBJDUMP="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-objdump"
    export CPPFLAGS="-I${libSystem}/usr/include"
    export CFLAGS="-isysroot $DARWIN_SDK_ROOT -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0"
    # NOTE: deliberately no "-Wl,-dylinker_install_name,/usr/lib/dyld" here.
    # That flag only makes sense for an executable's own LC_ID_DYLINKER; for
    # a shared library, ld64 was observed to use it as a fallback "install
    # path" whenever no correct -install_name reached it, stamping this
    # dylib's own LC_ID_DYLIB as "/usr/lib/dyld" - which then poisons every
    # consumer (e.g. i3) that links against it with the same bogus
    # dependency path ("Library not loaded: /usr/lib/dyld ... wrong
    # filetype", since /usr/lib/dyld is MH_DYLINKER, not MH_DYLIB, and can
    # never legally be loaded as a dependency). libtool's own darwin
    # dylib-link rules already compute a correct -install_name from
    # --libdir (set to /usr/lib above), so this flag isn't needed here.
    export LDFLAGS="-isysroot $DARWIN_SDK_ROOT -fuse-ld=${nativeLd}/bin/ld -nostdlib -L${libSystem}/usr/lib -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib -Wl,-platform_version,macos,11.0,11.5 -lSystem"

    # libtool bakes --libdir into each shared library's own -install_name
    # (LC_ID_DYLIB). Using the Nix store path ($out/usr/lib) here would
    # embed a build-time-only path into the dylib itself - same class of
    # bug as the puredarwin-git prefix issue. Point --libdir at the real
    # deployed path (/usr/lib) so libtool computes a correct install_name,
    # and redirect the actual file placement to $out/usr/lib separately at
    # install time below (DESTDIR-style decoupling).
    ./configure \
      --host=x86_64-apple-darwin20.4 \
      --build=$(cc -dumpmachine) \
      --prefix=/usr \
      --libdir=/usr/lib \
      --enable-shared \
      --enable-static \
      --disable-nls

    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    make -j$NIX_BUILD_CORES
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall

    make -C libcharset install \
      prefix="$out/usr" \
      exec_prefix="$out/usr" \
      libdir="$out/usr/lib"
    make -C lib install \
      prefix="$out/usr" \
      exec_prefix="$out/usr" \
      libdir="$out/usr/lib"
    mkdir -p "$out/usr/bin"
    install -m755 src/.libs/iconv_no_i18n "$out/usr/bin/iconv"
    mkdir -p "$out/usr/include"
    install -m644 include/iconv.h.inst "$out/usr/include/iconv.h"

    runHook postInstall
  '';

  dontFixup = true;

  meta = with lib; {
    platforms = platforms.linux;
  };
}
