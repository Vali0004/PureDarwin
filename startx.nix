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

    # startx-gop: launch the real Xorg server on the IOGOPFramebuffer (via the
    # puredarwingop driver) instead of the headless Xvfb, then run a client.
    cat > $out/bin/startx-gop <<'EOF'
#!/bin/sh
display=":0"
client="xeyes"

if [ "$#" -gt 0 ]; then
  client="$1"
  shift
fi

# Xorg reads /etc/X11/xorg.conf (Driver "puredarwingop"); it opens the GOP
# framebuffer user client itself, so no -screen geometry is needed.
#
# -ac disables access control: there is no .Xauthority in this image, so an
# authenticating server would refuse every client.
#
# Keep this script boring. The image's busybox ash is built with
# FEATURE_SH_MATH=n (no $((arith))) and a minimal builtin set, so no arithmetic,
# no kill -0 probing - just background the server, sleep, and run the client.
echo "startx-gop: starting Xorg on $display"
Xorg "$display" -config /etc/X11/xorg.conf -listen tcp -ac &
xpid=$!
trap 'kill "$xpid" 2>/dev/null' EXIT INT TERM

sleep 10

echo "startx-gop: launching $client on 127.0.0.1:0"
DISPLAY=127.0.0.1:0 exec "$client" "$@"
EOF
    chmod +x $out/bin/startx-gop
    runHook postInstall
  '';

  meta = with lib; {
    platforms = platforms.linux;
  };
}
