{
  description = ''
    PureDarwin's x86_64-apple-darwin cross toolchain (toolchain.nix) built
    entirely from nixpkgs' own unwrapped LLVM/clang/lld - no osxcross
    build.sh required - plus a smoke build (build.nix) proving it compiles
    and links real PureDarwin targets.

    packages.darwin-cross-toolchain: the toolchain itself, a drop-in
    alternative to /usr/local/osxcross/bin (see cmake/nix-toolchain.cmake).

    packages.default: builds helloapp + launchd through that toolchain as
    an integration smoke test. Deliberately NOT the full kernel+kext tree
    yet - that build takes hours and needs more Nix-sandbox iteration
    before it can run hermetically; use cmake/nix-toolchain.cmake +
    `cmake --build` directly for the full build in the meantime.
  '';

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-26.05";

  outputs = { self, nixpkgs }:
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
      smokeBuild = pkgs.callPackage ./build.nix { inherit darwinCrossToolchain; };
    in {
      packages.${system} = {
        darwin-cross-toolchain = darwinCrossToolchain;
        default = smokeBuild;
      };

      devShells.${system}.default = pkgs.mkShell {
        packages = [ darwinCrossToolchain pkgs.cmake pkgs.ninja ];
        NIX_DARWIN_TOOLCHAIN_DIR = "${darwinCrossToolchain}/bin";
        shellHook = ''
          echo "darwin-cross-toolchain-nix on PATH: x86_64-apple-darwin20.4-clang, xcrun, etc."
          echo "NIX_DARWIN_TOOLCHAIN_DIR exported for cmake/nix-toolchain.cmake"
        '';
      };
    };
}
