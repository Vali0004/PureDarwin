# launchd-dummy

Tiny userspace smoke-test binary for the early PureDarwin root filesystem.

```sh
ninja -C PureDarwin/build launchd-dummy
cmake --install PureDarwin/build --component BaseSystem --prefix "$PWD/PureDarwin/build-install"
```

launchd-dummy is because it's not a real launchd and can be nuked