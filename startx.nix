{ stdenv
, lib
, xvfb
, xeyes
}:

stdenv.mkDerivation {
  pname = "puredarwin-startx";
  version = "0.1";

  dontUnpack = true;
  dontPatchShebangs = true;

  installPhase = ''
    runHook preInstall
    mkdir -p $out/bin
    cat > $out/bin/startx <<'EOF'
#!/bin/sh
display=":1"
client="xeyes"

if [ "$#" -gt 0 ]; then
  client="$1"
  shift
fi

Xvfb "$display" -screen 0 800x600x24 -listen tcp &
xpid=$!
trap 'kill "$xpid" 2>/dev/null' EXIT INT TERM
sleep 1
DISPLAY=127.0.0.1:1 exec "$client" "$@"
EOF
    chmod +x $out/bin/startx
    runHook postInstall
  '';

  meta = with lib; {
    platforms = platforms.linux;
  };
}
