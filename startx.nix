{ stdenv
, lib
, xvfb
, xeyes
, xorg ? null
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
display=":0"
client="xeyes"

if [ "$#" -gt 0 ]; then
  client="$1"
  shift
fi
echo "startx: starting Xorg on $display"
Xorg "$display" -config /etc/X11/xorg.conf -listen tcp -ac &
xpid=$!
trap 'kill "$xpid" 2>/dev/null' EXIT INT TERM

sleep 10

echo "startx: launching $client on 127.0.0.1:0"
if [ "$client" = "xterm" ]; then
  set -- -xrm 'XTerm*backarrowKey: false' "$@"
fi
export DISPLAY=127.0.0.1:0
exec "$client" "$@"
EOF
    chmod +x $out/bin/startx
    runHook postInstall
  '';

  meta = with lib; {
    platforms = platforms.linux;
  };
}
