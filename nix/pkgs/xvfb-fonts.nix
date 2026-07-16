{ lib
, runCommand
, mkfontscale
, dejavu_fonts
}:

runCommand "puredarwin-fonts" { nativeBuildInputs = [ mkfontscale ]; } ''
  mkdir -p "$out/usr/share/fonts"
  cp ${dejavu_fonts}/share/fonts/truetype/*.ttf "$out/usr/share/fonts/"
  chmod u+w "$out/usr/share/fonts"/*.ttf

  # mkfontscale/mkfontdir generate the font path element's index files
  # (fonts.scale/fonts.dir) that libXfont2's directory-catalogue backend
  # needs to enumerate what's here; both tools just parse the font files
  # and emit text, so they run fine on the Linux build machine even though
  # the fonts themselves are for the (Darwin-target) Xvfb to serve.
  cd "$out/usr/share/fonts"
  mkfontscale .
  mkfontdir .
''
