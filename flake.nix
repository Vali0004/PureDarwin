{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    iig-tools.url = "github:Vali0004/iig-tools";
    kc-tools.url = "github:Vali0004/kc-tools";
  };

  outputs = { self, nixpkgs, iig-tools, kc-tools }:
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
      fullBuild = mkPureDarwinBuild {
        pname = "puredarwin";
        # "kexts" is the umbrella target over every src/Kernel/Extensions
        # kext (plain "all" is unusable: it also drags in tools/* as bogus
        # cross-compiled targets). The BaseSystem component install then
        # lays out the stage.sh/build-kc.sh-compatible tree.
        # libsystem_kernel: the dylib flavor has its own BaseSystem install
        # rule but nothing else in this set links it (they use the static
        # archive), so name it explicitly or the component install fails.
        buildTargets = [ "helloapp" "launchd" "busybox" "xnu" "kexts" "libsystem_kernel" ];
        installUserland = false;
        installKernel = false;
        installBaseSystem = true;
      };
      # Separate stage on top of the finished BaseSystem tree - assembling
      # the KC never touches or rebuilds the main build.
      kcBuild = pkgs.callPackage ./kc.nix {
        baseSystem = fullBuild;
        kcTools = kc-tools.packages.${system}.default;
      };
    in {
      packages.${system} = {
        darwin-cross-toolchain = darwinCrossToolchain;
        native-ld = nativeLd;
        userland = userlandBuild;
        kernel = kernelBuild;
        kc = kcBuild;
        default = fullBuild;
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
