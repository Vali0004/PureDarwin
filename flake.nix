{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    iig-tools.url = "github:Vali0004/iig-tools";
    kc-tools.url = "github:Vali0004/kc-tools";
    xnu-loader.url = "github:Vali0004/xnu-loader";
  };

  outputs = { self, nixpkgs, iig-tools, kc-tools, xnu-loader }:
    let
      lib = nixpkgs.lib;
      systems = [ "x86_64-linux" "x86_64-darwin" ];
      forAllSystems = lib.genAttrs systems;

      mkSystem = system:
        let
          pkgs = import nixpkgs {
            inherit system;
            config.allowUnfreePredicate = pkg: lib.getName pkg == "MacOSX11.3.sdk.tar.xz";
          };

          isDarwin = pkgs.stdenv.hostPlatform.isDarwin;
          iig = iig-tools.packages.${system}.default or (
            (pkgs.callPackage iig-tools { }).overrideAttrs (old: {
              meta = (old.meta or { }) // {
                platforms = pkgs.lib.platforms.unix;
              };
            })
          );
          darwinCrossToolchain = if isDarwin then null else pkgs.callPackage ./toolchain.nix { };
          libtapi = if isDarwin then null else pkgs.callPackage ./libtapi.nix { };
          nativeLd =
            if isDarwin then null else pkgs.callPackage ./native-ld.nix {
              inherit darwinCrossToolchain libtapi iig;
            };
          nativeUnifdef = if isDarwin then null else pkgs.callPackage ./unifdef.nix { };
          nativeMigcom = if isDarwin then null else pkgs.callPackage ./migcom.nix { };

          sourceWith = name: prefixes:
            lib.cleanSourceWith {
              src = ./.;
              filter = path: type:
                let
                  rel = lib.removePrefix "${toString ./.}/" (toString path);
                  isParentOfPrefix = prefix:
                    lib.hasPrefix "${rel}/" prefix;
                in
                  rel == "CMakeLists.txt"
                  || rel == "src/CMakeLists.txt"
                  || rel == "cmake"
                  || lib.hasPrefix "cmake/" rel
                  || lib.any (prefix:
                    rel == prefix
                    || lib.hasPrefix "${prefix}/" rel
                    || (type == "directory" && isParentOfPrefix prefix)
                  ) prefixes;
            };
          kernelSource = sourceWith "puredarwin-kernel-source" [
            "projects"
            "src/Kernel/CMakeLists.txt"
            "src/Kernel/xnu"
            "src/Kernel/libfirehose_kernel"
            "src/Libraries/CMakeLists.txt"
            "src/Libraries/AvailabilityVersions"
            "src/Libraries/libSystem/CMakeLists.txt"
            "src/Libraries/libSystem/libplatform"
            "src/Libraries/libSystem/pthread"
            "tools"
          ];
          libSystemSource = sourceWith "puredarwin-libsystem-source" [
            "src/Kernel/xnu/osfmk"
            "src/Kernel/xnu/libkern/libkern"
            "src/Kernel/xnu/libkern/os"
            "src/Kernel/xnu/bsd/i386"
            "src/Kernel/xnu/bsd/bsm"
            "src/Kernel/xnu/bsd/machine"
            "src/Kernel/xnu/bsd/net"
            "src/Kernel/xnu/bsd/netinet"
            "src/Kernel/xnu/bsd/netinet6"
            "src/Kernel/xnu/bsd/pthread"
            "src/Kernel/xnu/bsd/sys"
            "src/Kernel/xnu/bsd/sys_private"
            "src/Kernel/xnu/bsd/uuid"
            "src/Kernel/xnu/bsd/kern/makesyscalls.sh"
            "src/Kernel/xnu/bsd/kern/syscalls.master"
            "src/Libraries"
            "src/Libraries/libSystem/libmalloc/compat-include"
            "tools/mig"
          ];
          kextsSource = sourceWith "puredarwin-kexts-source" [
            "projects"
            "src/Kernel/CMakeLists.txt"
            "src/Kernel/Extensions"
            "src/Kernel/libkmod"
            "src/Kernel/xnu"
            "src/Kernel/libfirehose_kernel"
            "src/Libraries"
            "tools"
          ];
          userlandSource = sourceWith "puredarwin-userland-source" [
            "src/Kernel/xnu/osfmk"
            "src/Libraries/IOKit"
            "src/Libraries/PDGOP"
            "src/Libraries/libSystem/libmalloc/compat-include"
            "src/Libraries/libSystem/libsystem_kernel/mach"
            "src/Userspace"
            "tools/mig"
          ];

          mkPureDarwinBuild = args: pkgs.callPackage ./build.nix ({
            inherit darwinCrossToolchain nativeLd nativeUnifdef nativeMigcom iig;
          } // args);

          userlandBuild = mkPureDarwinBuild {
            pname = "puredarwin-userland";
            src = userlandSource;
            buildTargets = [ "helloapp" "launchd" "busybox" "sw_vers" "ps" "mkfile" "sync" "fbtri" "iokittest" ];
            enableProjects = false;
            enableKernel = false;
            enableLibraries = false;
            enableTools = false;
            installUserland = true;
            installKernel = false;
            prebuiltLibSystem = libSystemBuild;
          };
          tccBuild = mkPureDarwinBuild {
            pname = "puredarwin-tcc";
            src = userlandSource;
            buildTargets = [ "tcc" ];
            enableProjects = false;
            enableKernel = false;
            enableLibraries = false;
            enableTools = false;
            enableTcc = true;
            installUserland = true;
            installKernel = false;
            prebuiltLibSystem = libSystemBuild;
          };
          xvfbPixmanBuild =
            if isDarwin then null else pkgs.callPackage ./xvfb-pixman.nix {
              inherit darwinCrossToolchain nativeLd;
              libSystem = libSystemBuild;
              inherit (pkgs) pixman;
            };
          xvfbLibXauBuild =
            if isDarwin then null else pkgs.callPackage ./xvfb-stub-lib.nix {
              inherit darwinCrossToolchain;
              name = "Xau";
              version = pkgs.libxau.version or "1.0.12";
              pcName = "xau";
              pcDescription = "X authorization file management library";
              includeFrom = [ pkgs.libxau pkgs.xorgproto ];
              source = ''
                void *XauGetBestAuthByAddr(unsigned int family, unsigned int address_length, const char *address, unsigned int number_length, const char *number, int types_length, char **types, const int *type_lengths) { (void)family; (void)address_length; (void)address; (void)number_length; (void)number; (void)types_length; (void)types; (void)type_lengths; return 0; }
                void *XauReadAuth(const char *auth_file_name) { (void)auth_file_name; return 0; }
                void XauDisposeAuth(void *auth) { (void)auth; }
              '';
            };
          xvfbLibXdmcpBuild =
            if isDarwin then null else pkgs.callPackage ./xvfb-stub-lib.nix {
              inherit darwinCrossToolchain;
              name = "Xdmcp";
              version = pkgs.libxdmcp.version or "1.1.5";
              pcName = "xdmcp";
              pcDescription = "X Display Manager Control Protocol library";
              includeFrom = [ pkgs.libxdmcp pkgs.xorgproto ];
              source = ''
                int XdmcpWrap(const unsigned char *input, unsigned char *wrapper, const unsigned char *key) { (void)input; (void)wrapper; (void)key; return 0; }
                int XdmcpUnwrap(const unsigned char *input, unsigned char *wrapper, const unsigned char *key) { (void)input; (void)wrapper; (void)key; return 0; }
              '';
            };
          # nixpkgs' zlib is built for the Linux host (ELF); libfontenc's gzip
          # encoding-table reader and libXfont2's gunzip.c need a real Mach-O
          # zlib for the Darwin target.
          xvfbZlibBuild =
            if isDarwin then null else pkgs.callPackage ./xvfb-zlib.nix {
              inherit darwinCrossToolchain nativeLd;
              libSystem = libSystemBuild;
              inherit (pkgs) zlib;
            };
          # Real font stack (replaces the old no-op xfont2 stub): freetype2 ->
          # libfontenc -> libXfont2, all built with the same generic autotools
          # cross-helper used for libX11/libxcb. harfbuzz/png/bzip2/brotli are
          # disabled to keep the cross build's dependency surface small.
          freetype2Build =
            if isDarwin then null else pkgs.callPackage ./xvfb-freetype.nix {
              inherit darwinCrossToolchain nativeLd;
              libSystem = libSystemBuild;
              inherit (pkgs) zlib freetype;
            };
          libfontencBuild =
            if isDarwin then null else pkgs.callPackage ./xorg-cross-lib.nix {
              inherit darwinCrossToolchain nativeLd;
              libSystem = libSystemBuild;
              pname = "puredarwin-libfontenc";
              version = pkgs.libfontenc.version;
              src = pkgs.libfontenc.src;
              deps = [ pkgs.xorgproto xvfbZlibBuild ];
            };
          xvfbLibXfont2Build =
            if isDarwin then null else pkgs.callPackage ./xorg-cross-lib.nix {
              inherit darwinCrossToolchain nativeLd;
              libSystem = libSystemBuild;
              pname = "puredarwin-libXfont2";
              version = pkgs.libxfont_2.version;
              src = pkgs.libxfont_2.src;
              deps = [
                pkgs.xorgproto
                pkgs.xtrans
                xvfbZlibBuild
                freetype2Build
                libfontencBuild
              ];
              configureFlags = [
                "--disable-devel-docs"
              ];
            };
          xlibBuild =
            if isDarwin then null else pkgs.callPackage ./xorg-cross-lib.nix {
              inherit darwinCrossToolchain nativeLd;
              libSystem = libSystemBuild;
              pname = "puredarwin-libX11";
              version = pkgs.libX11.version;
              src = pkgs.libX11.src;
              deps = [
                pkgs.xorgproto
                pkgs.xtrans
                xcbBuild
                xvfbLibXauBuild
                xvfbLibXdmcpBuild
              ];
              configureFlags = [
                "--disable-specs"
              ];
            };
          xcbBuild =
            if isDarwin then null else pkgs.callPackage ./xorg-cross-lib.nix {
              inherit darwinCrossToolchain nativeLd;
              libSystem = libSystemBuild;
              pname = "puredarwin-libxcb";
              version = pkgs.libxcb.version;
              src = pkgs.libxcb.src;
              deps = [
                pkgs.xorgproto
                xvfbLibXauBuild
                xvfbLibXdmcpBuild
              ];
              nativeDeps = [
                pkgs.python3
                pkgs.xcb-proto
              ];
              configureFlags = [
                "--disable-devel-docs"
              ];
              preConfigureExtra = ''
                export PYTHONPATH="${pkgs.xcb-proto}/${pkgs.python3.sitePackages}:$PYTHONPATH"
              '';
            };
          # Real libxkbfile (nixpkgs' own is a Linux .so; ld already silently
          # "ignored" it as an incompatible file format on every xvfb build
          # so far -- Xvfb itself apparently never needed a real symbol from
          # it, but xkbcomp genuinely does).
          xvfbLibXkbfileBuild =
            if isDarwin then null else pkgs.callPackage ./xorg-cross-lib.nix {
              inherit darwinCrossToolchain nativeLd;
              libSystem = libSystemBuild;
              pname = "puredarwin-libxkbfile";
              version = pkgs.libxkbfile.version;
              src = pkgs.libxkbfile.src;
              deps = [ pkgs.xorgproto xlibBuild ];
            };
          xkbcompBuild =
            if isDarwin then null else pkgs.callPackage ./xvfb-xkbcomp.nix {
              inherit darwinCrossToolchain nativeLd;
              libSystem = libSystemBuild;
              inherit (pkgs) xkbcomp xorgproto;
              libX11 = xlibBuild;
              libxkbfile = xvfbLibXkbfileBuild;
              libXau = xvfbLibXauBuild;
              libXdmcp = xvfbLibXdmcpBuild;
              libxcb = xcbBuild;
            };
          # Actual font files for /usr/share/fonts -- the freetype2/libXfont2
          # backend can read these now, but nothing populated the directory
          # itself yet (it doesn't even exist in the image). mkfontscale and
          # mkfontdir just parse font files and emit text index files, so
          # they run fine on the Linux build machine.
          xvfbFontsBuild =
            if isDarwin then null else pkgs.callPackage ./xvfb-fonts.nix { };
          # xkeyboard-config's own output is plain share/X11/xkb (no usr/
          # prefix); repackage it to match this project's usr/-rooted image
          # layout, and to the /usr/share/X11/xkb path xkbcomp/Xvfb expect.
          xkeyboardConfigBuild =
            if isDarwin then null else pkgs.runCommand "puredarwin-xkeyboard-config" { } ''
              mkdir -p "$out/usr/share"
              cp -a ${pkgs.xkeyboard_config}/share/X11 "$out/usr/share/X11"
              chmod -R u+w "$out/usr/share/X11"
              cp -a ${pkgs.xkeyboard_config}/share/xkeyboard-config-2 "$out/usr/share/xkeyboard-config-2"
              chmod -R u+w "$out/usr/share/xkeyboard-config-2"
            '';
          xvfbBuild =
            if isDarwin then null else pkgs.callPackage ./xvfb.nix {
              inherit darwinCrossToolchain nativeLd;
              libSystem = libSystemBuild;
              xorg-server = pkgs.xorg-server;
              pixman = xvfbPixmanBuild;
              libXau = xvfbLibXauBuild;
              libXfont2 = xvfbLibXfont2Build;
              zlib = xvfbZlibBuild;
              freetype2 = freetype2Build;
              libfontenc = libfontencBuild;
              xvfbZlib = xvfbZlibBuild;
              inherit (pkgs) xorgproto xtrans;
              libxkbfile = xvfbLibXkbfileBuild;
              libXdmcp = pkgs.libxdmcp;
            };
          xeyesBuild =
            if isDarwin then null else pkgs.callPackage ./xeyes.nix {
              inherit darwinCrossToolchain nativeLd;
              libSystem = libSystemBuild;
              libX11 = xlibBuild;
              libxcb = xcbBuild;
              libXau = xvfbLibXauBuild;
              libXdmcp = xvfbLibXdmcpBuild;
              inherit (pkgs) xorgproto;
            };
          startxBuild =
            if isDarwin then null else pkgs.callPackage ./startx.nix {
              xvfb = xvfbBuild;
              xeyes = xeyesBuild;
            };
          libSystemBuild = mkPureDarwinBuild {
            pname = "puredarwin-libsystem";
            src = libSystemSource;
            buildTargets = [ "libSystem_B_stub" "dyld" "libsystem_kernel_static" ];
            enableUserspace = false;
            enableKernel = false;
            installUserland = false;
            installKernel = false;
            installLibSystem = true;
          };
          kernelBuild = mkPureDarwinBuild {
            pname = "puredarwin-kernel";
            src = kernelSource;
            buildTargets = [ "xnu" ];
            enableUserspace = false;
            installUserland = false;
            installKernel = true;
          };
          xnuHeadersBuild = mkPureDarwinBuild {
            pname = "puredarwin-xnu-headers";
            src = kernelSource;
            buildTargets = [ "xnu_headers.extproj" ];
            enableUserspace = false;
            installUserland = false;
            installKernel = false;
            installXnuHeaders = true;
          };
          kextsBuild = mkPureDarwinBuild {
            pname = "puredarwin-kexts";
            src = kextsSource;
            buildTargets = [ "kexts" ];
            enableUserspace = false;
            installUserland = false;
            installKernel = false;
            installKexts = true;
            enableIOGraphicsFamily = true;
          };
          iographicsBuild = mkPureDarwinBuild {
            pname = "puredarwin-iographics";
            src = kextsSource;
            buildTargets = [ "IOGraphicsFamily.kext" ];
            enableUserspace = false;
            installUserland = false;
            installKernel = false;
            installKexts = true;
            installKextNames = [ "IOGraphicsFamily.kext" ];
            enableIOGraphicsFamily = true;
          };
          fullBuild = mkPureDarwinBuild {
            pname = "puredarwin";
            src = ./.;
            buildTargets = [ "helloapp" "launchd" "busybox" "fbtri" "xnu" "kexts" "libsystem_kernel" ];
            installUserland = false;
            installKernel = false;
            installBaseSystem = true;
            enableIOGraphicsFamily = true;
          };
          splitBaseSystem = pkgs.runCommand "puredarwin-basesystem-split-0.1" { } (''
            mkdir -p "$out"
            cp -a ${kernelBuild}/. "$out/"
            chmod -R u+w "$out"
            cp -a ${kextsBuild}/. "$out/"
            chmod -R u+w "$out"
            cp -a ${libSystemBuild}/. "$out/"
            chmod -R u+w "$out"
            cp -a ${userlandBuild}/. "$out/"
            chmod -R u+w "$out"
            cp -a ${tccBuild}/. "$out/"
          ''
          + lib.optionalString (!isDarwin) ''
            chmod -R u+w "$out"
            cp -a ${xvfbBuild}/. "$out/"
            chmod -R u+w "$out"
            cp -a ${xeyesBuild}/. "$out/"
            chmod -R u+w "$out"
            cp -a ${startxBuild}/. "$out/"
            chmod -R u+w "$out"
            cp -a ${xkbcompBuild}/. "$out/"
            chmod -R u+w "$out"
            cp -a ${xkeyboardConfigBuild}/. "$out/"
            chmod -R u+w "$out"
            cp -a ${xvfbFontsBuild}/. "$out/"
          '');

          commonPackages = {
            userland = userlandBuild;
            tcc = tccBuild;
            libsystem = libSystemBuild;
            libSystem = libSystemBuild;
            xnu-headers = xnuHeadersBuild;
            xnu = kernelBuild;
            kernel = kernelBuild;
            kexts = kextsBuild;
            iographics = iographicsBuild;
            basesystem = fullBuild;
            basesystem-split = splitBaseSystem;
            default = fullBuild;
          } // lib.optionalAttrs (!isDarwin) {
            xvfb = xvfbBuild;
            libX11 = xlibBuild;
            libxcb = xcbBuild;
            xeyes = xeyesBuild;
            startx = startxBuild;
            libxkbfile = xvfbLibXkbfileBuild;
            xkbcomp = xkbcompBuild;
            xkeyboard-config = xkeyboardConfigBuild;
            fonts = xvfbFontsBuild;
          };

          linuxPackages =
            let
              kcBuild = pkgs.callPackage ./kc.nix {
                kernel = kernelBuild;
                kexts = kextsBuild;
                kcTools = kc-tools.packages.${system}.default;
              };
              imageBuild = pkgs.callPackage ./image.nix {
                baseSystem = splitBaseSystem;
                extraPackages = [
                  xvfbBuild
                  xeyesBuild
                  startxBuild
                  xkbcompBuild
                  xkeyboardConfigBuild
                  xvfbFontsBuild
                ];
                kc = kcBuild;
                xnuLoader = xnu-loader.packages.${system}.default;
              };
              runVm = pkgs.writeShellApplication {
                name = "puredarwin-vm";
                runtimeInputs = [ pkgs.qemu ];
                text = ''
                  set -euo pipefail

                  state_dir="''${PUREDARWIN_VM_STATE_DIR:-$PWD/.puredarwin-vm}"
                  image="''${PUREDARWIN_IMAGE:-}"
                  ovmf_code="''${PUREDARWIN_OVMF_CODE:-${pkgs.OVMF.fd}/FV/OVMF_CODE.fd}"
                  ovmf_vars_template="''${PUREDARWIN_OVMF_VARS_TEMPLATE:-${pkgs.OVMF.fd}/FV/OVMF_VARS.fd}"
                  ovmf_vars="''${PUREDARWIN_OVMF_VARS:-$state_dir/OVMF_VARS.fd}"

                  if [ -z "$image" ]; then
                    if [ -e "$PWD/puredarwin.img" ]; then
                      image="$PWD/puredarwin.img"
                    elif [ -e "$PWD/result/puredarwin.img" ]; then
                      image="$PWD/result/puredarwin.img"
                    else
                      echo "puredarwin-vm: no image found; set PUREDARWIN_IMAGE or run nix build .#image" >&2
                      exit 1
                    fi
                  fi

                  mkdir -p "$state_dir"
                  if [ ! -e "$ovmf_vars" ]; then
                    cp "$ovmf_vars_template" "$ovmf_vars"
                    chmod u+w "$ovmf_vars"
                  fi

                  exec qemu-system-x86_64 \
                    -M q35 \
                    -m "''${PUREDARWIN_VM_MEMORY:-4096}" \
                    -cpu IvyBridge,vendor=GenuineIntel \
                    -drive if=pflash,format=raw,unit=0,readonly=on,file="$ovmf_code" \
                    -drive if=pflash,format=raw,unit=1,file="$ovmf_vars" \
                    -drive id=root,format=raw,file="$image" \
                    -serial stdio \
                    -no-reboot \
                    -no-shutdown \
                    "$@"
                '';
              };
              runKvm = pkgs.writeShellApplication {
                name = "puredarwin-kvm";
                runtimeInputs = [ pkgs.qemu ];
                text = ''
                  set -euo pipefail

                  state_dir="''${PUREDARWIN_VM_STATE_DIR:-$PWD/.puredarwin-kvm}"
                  image="''${PUREDARWIN_IMAGE:-}"
                  ovmf_code="''${PUREDARWIN_OVMF_CODE:-${pkgs.OVMF.fd}/FV/OVMF_CODE.fd}"
                  ovmf_vars_template="''${PUREDARWIN_OVMF_VARS_TEMPLATE:-${pkgs.OVMF.fd}/FV/OVMF_VARS.fd}"
                  ovmf_vars="''${PUREDARWIN_OVMF_VARS:-$state_dir/OVMF_VARS.fd}"

                  if [ -z "$image" ]; then
                    if [ -e "$PWD/puredarwin.img" ]; then
                      image="$PWD/puredarwin.img"
                    elif [ -e "$PWD/result/puredarwin.img" ]; then
                      image="$PWD/result/puredarwin.img"
                    else
                      echo "puredarwin-kvm: no image found; set PUREDARWIN_IMAGE or run nix build .#image" >&2
                      exit 1
                    fi
                  fi

                  mkdir -p "$state_dir"
                  if [ ! -e "$ovmf_vars" ]; then
                    cp "$ovmf_vars_template" "$ovmf_vars"
                    chmod u+w "$ovmf_vars"
                  fi

                  exec qemu-system-x86_64 \
                    -machine q35,accel=kvm \
                    -cpu "''${PUREDARWIN_KVM_CPU:-host}" \
                    -smp "''${PUREDARWIN_VM_SMP:-4}" \
                    -m "''${PUREDARWIN_VM_MEMORY:-4096}" \
                    -drive if=pflash,format=raw,unit=0,readonly=on,file="$ovmf_code" \
                    -drive if=pflash,format=raw,unit=1,file="$ovmf_vars" \
                    -device ich9-ahci,id=sata \
                    -drive if=none,id=system,file="$image",format=raw,cache=writeback \
                    -device ide-hd,bus=sata.0,drive=system \
                    -device e1000-82545em,netdev=net0 \
                    -netdev user,id=net0 \
                    -usb \
                    -device usb-kbd \
                    -device usb-tablet \
                    -serial stdio \
                    -monitor none \
                    -no-reboot \
                    -no-shutdown \
                    "$@"
                '';
              };
            in {
              darwin-cross-toolchain = darwinCrossToolchain;
              native-ld = nativeLd;
              kc = kcBuild;
              image = imageBuild;
              vm-runner = runVm;
              kvm-runner = runKvm;
            };

          linuxApps =
            let
              runVm = linuxPackages.vm-runner;
              runKvm = linuxPackages.kvm-runner;
            in {
              default = {
                type = "app";
                program = "${runVm}/bin/puredarwin-vm";
              };
              vm = {
                type = "app";
                program = "${runVm}/bin/puredarwin-vm";
              };
              kvm = {
                type = "app";
                program = "${runKvm}/bin/puredarwin-kvm";
              };
            };

          devShell = pkgs.mkShell ({
            packages = [
              iig
              pkgs.cmake
              pkgs.ninja
              pkgs.bison
              pkgs.flex
              pkgs.perl
              pkgs.bash
              pkgs.ed
              pkgs.unifdef
              pkgs.tcsh
              pkgs.pax
              pkgs.coreutils
              pkgs.findutils
              pkgs.gawk
              pkgs.gnused
              pkgs.clang
              pkgs.ruby
            ] ++ lib.optionals (!isDarwin) [
              darwinCrossToolchain
              nativeMigcom
              nativeUnifdef
              pkgs.libuuid
            ];
          } // lib.optionalAttrs (!isDarwin) {
            NIX_DARWIN_TOOLCHAIN_DIR = "${darwinCrossToolchain}/bin";
            NIX_NATIVE_LD_PATH = "${nativeLd}/bin/ld";
            NIX_HOST_CC_PATH = "${pkgs.clang}/bin/clang";
            NIX_MIGCOM_PATH = "${nativeMigcom}/bin/migcom";
            NIX_UNIFDEF_PATH = "${nativeUnifdef}/bin/unifdef";
            PUREDARWIN_TCC_SOURCE = "${pkgs.tinycc.src}";
            shellHook = ''
              export CMAKE_TOOLCHAIN_FILE="$PWD/cmake/nix-toolchain.cmake"
              echo "PureDarwin Nix kernel shell: cmake/nix-toolchain.cmake and cached native ld/migcom/unifdef are active."
            '';
          } // lib.optionalAttrs isDarwin {
            shellHook = ''
              echo "PureDarwin Darwin shell: using the native Apple host toolchain path."
            '';
          });
        in {
          packages = commonPackages // lib.optionalAttrs (!isDarwin) linuxPackages;
          apps = lib.optionalAttrs (!isDarwin) linuxApps;
          devShells = {
            kernel = devShell;
            default = devShell;
          };
        };
    in {
      packages = forAllSystems (system: (mkSystem system).packages);
      apps = forAllSystems (system: (mkSystem system).apps);
      devShells = forAllSystems (system: (mkSystem system).devShells);
    };
}
