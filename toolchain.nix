# x86_64-apple-darwin cross toolchain built entirely from nixpkgs' own
# unwrapped LLVM/clang/lld - no osxcross build.sh, no compiling a second
# copy of LLVM from source. Verified end-to-end: nixpkgs clang emits real
# Mach-O objects for -target x86_64-apple-macosx*, and nixpkgs' ld64.lld
# links directly against Apple's .tbd (TAPI) stub libraries with no shims
# needed - and -fuse-ld=lld (resolved by PATH, not by absolute path - see
# compilerWrapper below) makes clang's Darwin driver auto-derive
# -arch/-platform_version/-syslibroot correctly, matching what osxcross's
# own from-source clang wrapper does.
#
# Produces osxcross-name-compatible wrapper binaries
# (x86_64-apple-darwin20.4-clang, -clang++, -ar, -ranlib, -strip, -nm, -ld,
# -dsymutil, -install_name_tool, -lipo, plus an xcrun shim) so it's a
# drop-in alternative to /usr/local/osxcross/bin, and is usable directly
# via cmake/nix-toolchain.cmake.
{ lib
, writeShellScriptBin
, symlinkJoin
, llvmPackages_21
, target ? "x86_64-apple-darwin20.4"
, clangTarget ? "x86_64-apple-macosx11.0"
, defaultSdkRoot ? "/usr/local/osxcross/SDK/MacOSX11.3.sdk"
}:

let
  clang = llvmPackages_21.clang-unwrapped;
  lld = llvmPackages_21.lld;
  bintools = llvmPackages_21.bintools-unwrapped;

  # -fuse-ld must be the *name* "lld", not an absolute path to ld64.lld:
  # clang's Darwin driver only auto-derives -arch/-platform_version/
  # -syslibroot for the linker job when it recognizes the linker as its own
  # lld integration (name-based dispatch); pointed at an arbitrary absolute
  # binary path it falls back to a bare invocation and ld64.lld then fails
  # with "missing -platform_version"/"missing -arch". So resolve it by
  # PATH instead, with lld's bin dir prepended.
  compilerWrapper = name: realBin: writeShellScriptBin "${target}-${name}" ''
    SDK="''${DARWIN_SDK_ROOT:-${defaultSdkRoot}}"
    export PATH="${lld}/bin:$PATH"
    exec ${realBin} \
      -target ${clangTarget} \
      -isysroot "$SDK" \
      -fuse-ld=lld \
      "$@"
  '';

  simpleWrapper = name: realBin: writeShellScriptBin "${target}-${name}" ''
    exec ${realBin} "$@"
  '';

  ldWrapper = writeShellScriptBin "${target}-ld" ''
    exec ${lld}/bin/ld64.lld "$@"
  '';

  # osxcross also exposes a bare (non-triple-prefixed) "dsymutil" on PATH,
  # which some CMakeLists (e.g. src/Kernel/xnu) look for via
  # find_program(... NAMES dsymutil llvm-dsymutil). Match that.
  bareDsymutil = writeShellScriptBin "dsymutil" ''
    exec ${bintools}/bin/dsymutil "$@"
  '';

  # Minimal xcrun: `xcrun [-sdk macosx] TOOL ARGS...` -> our wrapped TOOL, or
  # a handful of direct queries (-show-sdk-*) that xnu's own makedefs
  # (cmake/MakeInc.cmd.in) issue directly rather than dispatching to a tool.
  # -find falls back past our own toolchain bin dir to plain PATH lookup,
  # since some tools makedefs asks for (mig, migcom, unifdef, libtool) are
  # the project's own build products / nixpkgs-native tools, not part of
  # this cross toolchain.
  xcrunShim = writeShellScriptBin "xcrun" ''
    set -e
    BINDIR="$(cd "$(dirname "$0")" && pwd)"
    SDK="''${DARWIN_SDK_ROOT:-${defaultSdkRoot}}"
    while [ $# -gt 0 ]; do
      case "$1" in
        -sdk|--sdk) shift 2 ;;
        -show-sdk-path|--show-sdk-path) echo "$SDK"; exit 0 ;;
        -show-sdk-platform-path|--show-sdk-platform-path) echo "$SDK/.."; exit 0 ;;
        -show-sdk-version|--show-sdk-version) echo "11.3"; exit 0 ;;
        -find|--find)
          # -find PRINTS the resolved path (matching real xcrun - callers
          # like xnu's makedefs do `export MIG := $(shell xcrun -find mig)`
          # and expect text output, not execution).
          shift
          # cc/c++ aren't wrapper names we ship - MIG and friends invoke
          # bare "cc", which should mean our clang wrapper, same as real
          # Apple/osxcross xcrun -find cc.
          case "$1" in
            cc) find_name=clang ;;
            c++) find_name=clang++ ;;
            *) find_name="$1" ;;
          esac
          if command -v "${target}-$find_name" >/dev/null 2>&1; then
            command -v "${target}-$find_name"
          else
            command -v "$1"
          fi
          exit 0
          ;;
        -log|-v) shift ;;
        *) break ;;
      esac
    done
    tool="$1"; shift
    case "$tool" in
      cc) wrapped=clang ;;
      c++) wrapped=clang++ ;;
      *) wrapped="$tool" ;;
    esac
    # `exec --` (not bare `exec`): if $wrapped/$tool ever starts with "-"
    # (has happened with a literal "--" token), bash's exec builtin
    # misparses it as ITS OWN option rather than the command to run.
    if [ -x "$BINDIR/${target}-$wrapped" ]; then
      exec -- "$BINDIR/${target}-$wrapped" "$@"
    fi
    exec -- "$tool" "$@"
  '';

  tools = [
    (compilerWrapper "clang" "${clang}/bin/clang")
    (compilerWrapper "clang++" "${clang}/bin/clang++")
    (simpleWrapper "ar" "${bintools}/bin/llvm-ar")
    (simpleWrapper "ranlib" "${bintools}/bin/llvm-ranlib")
    (simpleWrapper "strip" "${bintools}/bin/llvm-strip")
    (simpleWrapper "nm" "${bintools}/bin/llvm-nm")
    (simpleWrapper "objdump" "${bintools}/bin/llvm-objdump")
    (simpleWrapper "dsymutil" "${bintools}/bin/dsymutil")
    (simpleWrapper "install_name_tool" "${bintools}/bin/llvm-install-name-tool")
    (simpleWrapper "lipo" "${bintools}/bin/llvm-lipo")
    ldWrapper
  ];
in
symlinkJoin {
  name = "darwin-cross-toolchain-nix";
  paths = tools ++ [ xcrunShim bareDsymutil ];

  meta = with lib; {
    description = "x86_64-apple-darwin cross toolchain built from nixpkgs' unwrapped LLVM, no osxcross build.sh required";
    platforms = platforms.linux;
  };
}
