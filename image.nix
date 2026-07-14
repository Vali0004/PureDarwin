{ stdenv
, lib
, baseSystem
, extraPackages ? [ ]
, kc
, xnuLoader
, gptfdisk
, util-linux
, dosfstools
, mtools
, e2fsprogs
, espMB ? 64
, rootMB ? 640
}:

assert lib.isDerivation baseSystem;
assert lib.all lib.isDerivation extraPackages;

stdenv.mkDerivation {
  pname = "puredarwin-image";
  version = "0.1";

  dontUnpack = true;

  nativeBuildInputs = [ gptfdisk util-linux dosfstools mtools e2fsprogs ];

  buildPhase = ''
    runHook preBuild
    export MTOOLS_SKIP_CHECK=1
    LINUX_FS_GUID=0FC63DAF-8483-4772-8E79-3D69D8477DE4

    img=puredarwin.img
    esp_sectors=$((${toString espMB} * 2048))
    img_sectors=$((2048 + esp_sectors + ${toString rootMB} * 2048 + 2048))
    truncate -s $((img_sectors * 512)) $img
    sgdisk \
      -n 1:2048:+${toString espMB}M -t 1:EF00            -c 1:"EFI System Partition" \
      -n 2:0:+${toString rootMB}M   -t 2:$LINUX_FS_GUID  -c 2:"Darwin ext4 Root" \
      $img >/dev/null

    read -r esp_start esp_size <<<"$(sfdisk -d $img | grep -i type=C12A7328 \
      | sed -E 's/.*start= *([0-9]+), *size= *([0-9]+),.*/\1 \2/')"
    read -r root_start root_size <<<"$(sfdisk -d $img | grep -i type=$LINUX_FS_GUID \
      | sed -E 's/.*start= *([0-9]+), *size= *([0-9]+),.*/\1 \2/')"

    truncate -s $((esp_size * 512)) esp.img
    mkfs.vfat -F 32 -n EFI esp.img >/dev/null
    mmd -i esp.img ::/EFI ::/EFI/BOOT
    mcopy -o -i esp.img ${xnuLoader}/img/EFI/BOOT/BOOTX64.EFI ::/EFI/BOOT/BOOTX64.EFI
    mcopy -o -i esp.img ${kc}/kernel                          ::/EFI/BOOT/kernel
    dd if=esp.img of=$img bs=512 seek=$esp_start count=$esp_size conv=notrunc status=none

    staging="$PWD/root-staging"
    mkdir -p "$staging"

    copyPackage() {
      local package="$1"

      echo "Installing $package into image staging tree"

      if [ ! -d "$package" ]; then
        echo "error: package path does not exist or is not a directory: $package" >&2
        exit 1
      fi

      cp -a -- "$package"/. "$staging"/
      chmod -R u+rwX "$staging"
    }

    copyPackage "${baseSystem}";

    ${lib.concatMapStringsSep "\n" (package: ''
      copyPackage "${package}";
    '') extraPackages}
    for dir in \
      usr \
      usr/bin \
      usr/lib \
      usr/lib/system \
      usr/sbin \
      usr/share \
      usr/share/X11 \
      bin \
      sbin \
      dev \
      etc \
      tmp \
      var \
      var/root \
      var/run \
      var/log \
      var/tmp \
      var/empty \
      tmp/.X11-unix
    do
      mkdir -p "$staging/$dir"
    done

    cat > $staging/etc/passwd <<'EOF'
root:*:0:0:System Administrator:/var/root:/bin/sh
daemon:*:1:1:System Services:/var/root:/usr/bin/false
nobody:*:-2:-2:Unprivileged User:/var/empty:/usr/bin/false
EOF
    cat > $staging/etc/group <<'EOF'
wheel:*:0:root
daemon:*:1:root
nogroup:*:-1:
nobody:*:-2:
staff:*:20:root
EOF
    cat > $staging/etc/profile <<'EOF'
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export PS1='\u@\h:\w \$ '
EOF
    chmod 644 $staging/etc/passwd $staging/etc/group $staging/etc/profile

    if [ -x $staging/bin/helloapp ] && [ ! -e $staging/sbin/helloapp ]; then
      ln -sf /bin/helloapp $staging/sbin/helloapp
    fi

    for applet in \
      ash \
      awk \
      basename \
      cat \
      chmod \
      clear \
      cmp \
      cp \
      cut \
      date \
      dd \
      df \
      diff \
      dirname \
      du \
      echo \
      env \
      expr \
      false \
      find \
      grep \
      gunzip \
      gzip \
      head \
      hostname \
      id \
      kill \
      ln \
      ls \
      mkdir \
      mknod \
      more \
      mv \
      patch \
      printf \
      pwd \
      readlink \
      realpath \
      reset \
      rm \
      rmdir \
      sed \
      seq \
      sh \
      sleep \
      sort \
      stat \
      stty \
      tail \
      tar \
      test \
      touch \
      tr \
      true \
      truncate \
      tty \
      uname \
      uniq \
      vi \
      wc \
      which \
      whoami \
      xargs \
      yes
    do
      ln -s busybox "$staging/bin/$applet"
    done

    for applet in reboot shutdown; do
      ln -sf /bin/busybox $staging/sbin/$applet
    done

    chmod 1777 \
      "$staging/tmp" \
      "$staging/var/tmp" \
      "$staging/tmp/.X11-unix"

    chmod 700  $staging/var/root

    echo "Image X11 executables:"
    for executable in Xvfb xeyes startx; do
      found=
      for candidate in \
        "$staging/bin/$executable" \
        "$staging/usr/bin/$executable"
      do
        if [ -x "$candidate" ]; then
          echo "  $candidate"
          found=1
          break
        fi
      done

      if [ -z "$found" ]; then
        echo "warning: $executable was not installed into the image" >&2
      fi
    done

    truncate -s $((root_size * 512)) root.img

    # ext4.kext is R/W and handles 64-bit block numbers + metadata
    # checksums now; only the journal (and orphan_file) stay off.
    mke2fs -q -F -t ext4 \
      -b 4096 \
      -O ^has_journal,^orphan_file \
      -L darwin-ext4 \
      -d $staging \
      root.img >/dev/null

    dd if=root.img of=$img bs=512 seek=$root_start count=$root_size conv=notrunc status=none

    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p $out
    cp puredarwin.img $out/puredarwin.img
    runHook postInstall
  '';

  meta = with lib; {
    description = "Bootable PureDarwin GPT disk image (xnu-loader ESP + kc-tools kernel collection + ext4 BaseSystem root)";
    platforms = platforms.linux;
  };
}
