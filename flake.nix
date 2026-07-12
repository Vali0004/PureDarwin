{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    iig-tools.url = "github:Vali0004/iig-tools";
    kc-tools.url = "github:Vali0004/kc-tools";
    xnu-loader.url = "github:Vali0004/xnu-loader";
  };

  outputs = { self, nixpkgs, iig-tools, kc-tools, xnu-loader }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs {
        inherit system;
        config.allowUnfreePredicate = pkg: nixpkgs.lib.getName pkg == "MacOSX11.3.sdk.tar.xz";
      };

      darwinCrossToolchain = pkgs.callPackage ./toolchain.nix { };
      libtapi = pkgs.callPackage ./libtapi.nix { };
      nativeLd = pkgs.callPackage ./native-ld.nix {
        inherit darwinCrossToolchain libtapi;
        iig = iig-tools.packages.${system}.default;
      };
      nativeUnifdef = pkgs.callPackage ./unifdef.nix { };
      nativeMigcom = pkgs.callPackage ./migcom.nix { };
      mkPureDarwinBuild = args: pkgs.callPackage ./build.nix ({
        inherit darwinCrossToolchain nativeLd nativeUnifdef nativeMigcom;
        iig = iig-tools.packages.${system}.default;
      } // args);
      userlandBuild = mkPureDarwinBuild {
        pname = "puredarwin-userland";
        buildTargets = [ "helloapp" "launchd" "busybox" ];
        installUserland = true;
        installKernel = false;
      };
      kernelBuild = mkPureDarwinBuild {
        pname = "puredarwin-kernel";
        buildTargets = [ "xnu" ];
        installUserland = false;
        installKernel = true;
      };
      xnuHeadersBuild = mkPureDarwinBuild {
        pname = "puredarwin-xnu-headers";
        buildTargets = [ "xnu_headers.extproj" ];
        installUserland = false;
        installKernel = false;
        installXnuHeaders = true;
      };
      kextsBuild = mkPureDarwinBuild {
        pname = "puredarwin-kexts";
        buildTargets = [ "kexts" ];
        installUserland = false;
        installKernel = false;
        installKexts = true;
      };
      fullBuild = mkPureDarwinBuild {
        pname = "puredarwin";
        buildTargets = [ "helloapp" "launchd" "busybox" "xnu" "kexts" "libsystem_kernel" ];
        installUserland = false;
        installKernel = false;
        installBaseSystem = true;
      };
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
      packages.${system} = {
        darwin-cross-toolchain = darwinCrossToolchain;
        native-ld = nativeLd;
        userland = userlandBuild;
        xnu-headers = xnuHeadersBuild;
        xnu = kernelBuild;
        kernel = kernelBuild;
        kexts = kextsBuild;
        basesystem = fullBuild;
        kc = kcBuild;
        image = imageBuild;
        vm-runner = runVm;
        default = fullBuild;
      };

      apps.${system} = {
        default = {
          type = "app";
          program = "${runVm}/bin/puredarwin-vm";
        };
        vm = {
          type = "app";
          program = "${runVm}/bin/puredarwin-vm";
        };
      };

      devShells.${system} = rec {
        kernel = pkgs.mkShell {
          packages = [
            darwinCrossToolchain
            nativeMigcom
            nativeUnifdef
            iig-tools.packages.${system}.default
            pkgs.cmake
            pkgs.ninja
            pkgs.libuuid
          ];
          NIX_DARWIN_TOOLCHAIN_DIR = "${darwinCrossToolchain}/bin";
          NIX_NATIVE_LD_PATH = "${nativeLd}/bin/ld";
          NIX_HOST_CC_PATH = "${pkgs.clang}/bin/clang";
          NIX_MIGCOM_PATH = "${nativeMigcom}/bin/migcom";
          NIX_UNIFDEF_PATH = "${nativeUnifdef}/bin/unifdef";
          shellHook = ''
            export CMAKE_TOOLCHAIN_FILE="$PWD/cmake/nix-toolchain.cmake"
            echo "PureDarwin Nix kernel shell: cmake/nix-toolchain.cmake and cached native ld/migcom/unifdef are active."
          '';
        };
        default = kernel;
      };
    };
}
