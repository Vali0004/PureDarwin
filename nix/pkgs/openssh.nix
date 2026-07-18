{ stdenv
, lib
, requireFile
, darwinCrossToolchain
, nativeLd
, libSystem
, openssh
, openssl
, zlib
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
  pname = "puredarwin-openssh";
  inherit (openssh) version;
  src = openssh.src;

  postPatch = ''
    # Darwin's stdlib.h declares arc4random_stir(void). When configure decides
    # it is unavailable, OpenSSH's compat header turns it into a function-like
    # macro; later SDK declarations then fail to parse. PureDarwin does not
    # currently export arc4random_stir, so keep it unavailable but don't poison
    # the SDK headers.
    substituteInPlace openbsd-compat/openbsd-compat.h \
      --replace-fail '# define arc4random_stir()' '# include <stdlib.h>
# define arc4random_stir()'

    # PureDarwin does not export ptrace or setgroups yet. Avoid OpenSSH's
    # Darwin-specific anti-trace call, and keep uid group swapping disabled
    # until libSystem has a real setgroups wrapper.
    substituteInPlace platform-tracing.c \
      --replace-fail '#ifdef PT_DENY_ATTACH' '#if defined(PT_DENY_ATTACH) && !defined(__APPLE__)'
    substituteInPlace uidswap.c \
      --replace-fail 'if (setgroups(user_groupslen, user_groups) == -1)' 'if (0)' \
      --replace-fail 'if (setgroups(saved_egroupslen, saved_egroups) == -1)' 'if (0)'
    substituteInPlace sshd-auth.c \
      --replace-fail 'if (setgroups(1, gidset) == -1)' 'if (0)'
    substituteInPlace sshd.c \
      --replace-fail 'if (geteuid() == 0 && setgroups(0, NULL) == -1)' 'if (0)' \
      --replace-fail 'if (setgroups(0, NULL) < 0)' 'if (0)'
  '';

  configurePhase = ''
    runHook preConfigure

    mkdir -p sdk
    tar xf ${sdkTarball} -C sdk
    export DARWIN_SDK_ROOT="$PWD/sdk/MacOSX11.3.sdk"
    export PATH="${darwinCrossToolchain}/bin:$PATH"
    export CC="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-clang"
    export AR="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-ar"
    export RANLIB="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-ranlib"
    export STRIP="${darwinCrossToolchain}/bin/x86_64-apple-darwin20.4-strip"
    export CPPFLAGS="-I${libSystem}/usr/include -I${openssl}/include -I${zlib}/include"
    export CFLAGS="-isysroot $DARWIN_SDK_ROOT -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0"
    export LDFLAGS="-isysroot $DARWIN_SDK_ROOT -fuse-ld=${nativeLd}/bin/ld -nostdlib -Wl,-Z -L${libSystem}/usr/lib -L${openssl}/lib -L${zlib}/lib -Wl,-dylib_file,/usr/lib/system/libdyld.dylib:${libSystem}/usr/lib/system/libdyld.dylib -Wl,-dylinker_install_name,/usr/lib/dyld -Wl,-platform_version,macos,11.0,11.5 -lSystem"
    export LIBS="-Wl,-force_load,${openssl}/lib/libcrypto.a -Wl,-force_load,${zlib}/lib/libz.a -lSystem"

    export ac_cv_func_bcopy=yes
    export ac_cv_func_bzero=yes
    export ac_cv_func_b64_ntop=no
    export ac_cv_func___b64_ntop=no
    export ac_cv_func_b64_pton=no
    export ac_cv_func___b64_pton=no
    export ac_cv_func_arc4random=yes
    export ac_cv_func_arc4random_buf=yes
    export ac_cv_func_arc4random_stir=no
    export ac_cv_func_arc4random_uniform=no
    export ac_cv_func_bcrypt_pbkdf=no
    export ac_cv_func_closefrom=no
    export ac_cv_func_close_range=no
    export ac_cv_func_explicit_bzero=no
    export ac_cv_func_freezero=no
    export ac_cv_func_fstatvfs=yes
    export ac_cv_func_getaddrinfo=yes
    export ac_cv_func_getentropy=no
    export ac_cv_func_getrandom=no
    export ac_cv_func_getnameinfo=yes
    export ac_cv_func_getpagesize=yes
    export ac_cv_func_getpeereid=no
    export ac_cv_func_getpeerucred=no
    export ac_cv_func_getspnam=no
    export ac_cv_func_gettimeofday=yes
    export ac_cv_func_innetgr=no
    export ac_cv_func_login_getcapbool=no
    export ac_cv_func_login_getpwclass=no
    export ac_cv_func_mmap=yes
    export ac_cv_func_proc_pidinfo=no
    export ac_cv_func_reallocarray=no
    export ac_cv_func_realpath=yes
    export ac_cv_func_recallocarray=no
    export ac_cv_func_rresvport_af=yes
    export ac_cv_func_sandbox_init=no
    export ac_cv_func_select=yes
    export ac_cv_func_setenv=yes
    export ac_cv_func_setlogin=no
    export ac_cv_func_setproctitle=no
    export ac_cv_func_sigaction=yes
    export ac_cv_func_snprintf=yes
    export ac_cv_func_statvfs=yes
    export ac_cv_func_strlcpy=yes
    export ac_cv_func_strmode=no
    export ac_cv_func_strnvis=no
    export ac_cv_func_strtonum=no
    export ac_cv_func_sysconf=yes
    export ac_cv_func_unsetenv=yes
    export ac_cv_func_vsnprintf=yes
    export ac_cv_func_EVP_CIPHER_CTX_get_iv=no
    export ac_cv_func_EVP_CIPHER_CTX_get_updated_iv=no
    export ac_cv_func_EVP_CIPHER_CTX_set_iv=no
    export ac_cv_have_decl_HOWMANY=no

    ./configure \
      --host=x86_64-apple-darwin20.4 \
      --build=$(cc -dumpmachine) \
      --prefix=$out \
      --sysconfdir=$out/etc/ssh \
      --with-privsep-path=$out/var/empty \
      --with-ssl-dir=${openssl} \
      --with-zlib=${zlib} \
      --without-openssl-header-check \
      --without-pam \
      --without-kerberos5 \
      --without-ldns \
      --without-libedit \
      --without-audit \
      --without-selinux \
      --disable-utmp \
      --disable-utmpx \
      --disable-wtmp \
      --disable-wtmpx \
      --disable-lastlog

    sed -i '/^#[[:space:]]*define[[:space:]]*arc4random_stir()$/c\
#include <stdlib.h>\
#define arc4random_stir()' defines.h
    substituteInPlace Makefile \
      --replace-fail '$(INSTALL) -m 4711 $(STRIP_OPT) ssh-keysign$(EXEEXT) $(DESTDIR)$(SSH_KEYSIGN)$(EXEEXT)' \
                     '$(INSTALL) -m 0755 $(STRIP_OPT) ssh-keysign$(EXEEXT) $(DESTDIR)$(SSH_KEYSIGN)$(EXEEXT)'

    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    make -j$NIX_BUILD_CORES
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    make install-nokeys STRIP_OPT=
    runHook postInstall
  '';

  dontFixup = true;
  dontStrip = true;

  meta = with lib; {
    platforms = platforms.linux;
  };
}
