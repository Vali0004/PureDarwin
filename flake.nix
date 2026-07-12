{
  description = ''
    PureDarwin's x86_64-apple-darwin cross toolchain (toolchain.nix) built
    entirely from nixpkgs' own unwrapped LLVM/clang/lld - no osxcross
    build.sh required - plus Nix build targets proving it compiles and
    links real PureDarwin targets.

    packages.darwin-cross-toolchain: the toolchain itself, a drop-in
    alternative to /usr/local/osxcross/bin (see cmake/nix-toolchain.cmake).

    packages.userland: builds helloapp + launchd through that toolchain.
    packages.kernel: builds the XNU install target.
    packages.default: builds both userland and kernel targets.
  '';

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";
    iig-tools.url = "github:Vali0004/iig-tools";
  };

  outputs = { self, nixpkgs, iig-tools }:
    let
      system = "x86_64-linux";
      # requireFile (used by build.nix for the proprietary Apple SDK
      # tarball) is marked unfree by nixpkgs; this is a locally-registered,
      # non-redistributed input, not something fetched from a remote
      # mirror, so allow it.
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
