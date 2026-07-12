{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    iig-tools.url = "github:Vali0004/iig-tools";
  };

  outputs = { self, nixpkgs, iig-tools }:
    let
      system = "x86_64-linux";
      pkgs = import nixpkgs {
        inherit system;
        config.allowUnfreePredicate = pkg: nixpkgs.lib.getName pkg == "MacOSX11.3.sdk.tar.xz";
      };

      darwinCrossToolchain = pkgs.callPackage ./toolchain.nix { };
      mkPureDarwinBuild = args: pkgs.callPackage ./build.nix ({
        inherit darwinCrossToolchain;
        iig = iig-tools.packages.${system}.default;
      } // args);
      userlandBuild = mkPureDarwinBuild {
        pname = "puredarwin-userland";
        # Busybox requires some... unsupported ld64.lld flags
        buildTargets = [ "helloapp" "launchd" ];
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
        buildTargets = [ "helloapp" "launchd" "xnu" ];
        installUserland = true;
        installKernel = true;
      };
    in {
      packages.${system} = {
        darwin-cross-toolchain = darwinCrossToolchain;
        userland = userlandBuild;
        kernel = kernelBuild;
        default = fullBuild;
      };

      devShells.${system}.default = pkgs.mkShell {
        packages = [ darwinCrossToolchain iig-tools.packages.${system}.default pkgs.cmake pkgs.ninja ];
        NIX_DARWIN_TOOLCHAIN_DIR = "${darwinCrossToolchain}/bin";
        shellHook = ''
          echo "darwin-cross-toolchain-nix on PATH: x86_64-apple-darwin20.4-clang, xcrun, etc."
          echo "NIX_DARWIN_TOOLCHAIN_DIR exported for cmake/nix-toolchain.cmake"
        '';
      };
    };
}
