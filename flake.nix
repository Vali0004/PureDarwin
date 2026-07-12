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
            "src/Kernel"
            "src/Libraries"
            "tools"
          ];
          userlandSource = sourceWith "puredarwin-userland-source" [
            "src/Userspace"
          ];

          mkPureDarwinBuild = args: pkgs.callPackage ./build.nix ({
            inherit darwinCrossToolchain nativeLd nativeUnifdef nativeMigcom iig;
          } // args);

          userlandBuild = mkPureDarwinBuild {
            pname = "puredarwin-userland";
            src = userlandSource;
            buildTargets = [ "helloapp" "launchd" "busybox" "tcc" ];
            enableProjects = false;
            enableKernel = false;
            enableLibraries = false;
            enableTools = false;
            installUserland = true;
            installKernel = false;
            prebuiltLibSystem = libSystemBuild;
          };
          libSystemBuild = mkPureDarwinBuild {
            pname = "puredarwin-libsystem";
            src = kernelSource;
            buildTargets = [ "libSystem_B_stub" "dyld" ];
            enableUserspace = false;
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
            src = kernelSource;
            buildTargets = [ "kexts" ];
            enableUserspace = false;
            installUserland = false;
            installKernel = false;
            installKexts = true;
          };
          fullBuild = mkPureDarwinBuild {
            pname = "puredarwin";
            src = ./.;
            buildTargets = [ "helloapp" "launchd" "busybox" "tcc" "xnu" "kexts" "libsystem_kernel" ];
            installUserland = false;
            installKernel = false;
            installBaseSystem = true;
          };

          commonPackages = {
            userland = userlandBuild;
            libsystem = libSystemBuild;
            libSystem = libSystemBuild;
            xnu-headers = xnuHeadersBuild;
            xnu = kernelBuild;
            kernel = kernelBuild;
            kexts = kextsBuild;
            basesystem = fullBuild;
            default = fullBuild;
          };

          linuxPackages =
            let
              kcBuild = pkgs.callPackage ./kc.nix {
                baseSystem = fullBuild;
                kcTools = kc-tools.packages.${system}.default;
              };
              imageBuild = pkgs.callPackage ./image.nix {
                baseSystem = fullBuild;
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
            in {
              darwin-cross-toolchain = darwinCrossToolchain;
              native-ld = nativeLd;
              kc = kcBuild;
              image = imageBuild;
              vm-runner = runVm;
            };

          linuxApps =
            let
              runVm = linuxPackages.vm-runner;
            in {
              default = {
                type = "app";
                program = "${runVm}/bin/puredarwin-vm";
              };
              vm = {
                type = "app";
                program = "${runVm}/bin/puredarwin-vm";
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
