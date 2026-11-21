# Developer guide

This repository is the C++23 **engine** behind the Mango Streaming Server apps (media library, ffmpeg transcoding to DASH and HLS, Chromecast discovery and control, and the HTTP media server renderers fetch streams from), and `Mango`, a console server built on it with no UI.

## Layout

| Folder | What's in it |
|---|---|
| `src/` | The engine. `Engine.hpp` is its only public header. |
| `app/` | `Mango`, the console server. |
| `tests/` | Engine tests (`MangoTests`). |
| `cmake/` | Build helpers, the Linux package rules and the vcpkg triplet overlay. |
| `packaging/` | The Linux systemd user service, the Linux install script and the ffmpeg license check. |
| `signal/` | The signaling server for remote streaming (Node.js); see its README. |

## Requirements

- CMake 3.25 or later
- [vcpkg](https://vcpkg.io), with `VCPKG_ROOT` set to its folder. It builds ffmpeg and the other dependencies from source. Their versions are pinned by `builtin-baseline` in `vcpkg.json` (a vcpkg commit); move it deliberately, together with a build and test run.
- A C++23 compiler. The presets use MacPorts `clang++-mp-22` on macOS (arm64), `clang-cl` on Windows (x64) and `clang++-22` on Linux (x64).

**Linux:** see [LINUX.md](LINUX.md) for the full setup, build, package and service guide.

The first build is slow: vcpkg builds ffmpeg and the other dependencies, which takes tens of minutes. After that, rebuilds only compile what changed.

## Engine and console server

macOS:

```sh
cmake --preset macos-clang          # once; macos-clang-release for Release
cmake --build --preset macos        # Debug; macos-arm64-release for Release
./build/Mango
```

Windows:

```sh
cmake --preset windows-clangcl
cmake --build --preset windows-clangcl-x64-debug   # or windows-clangcl-x64-release
```

Linux: see [LINUX.md](LINUX.md).

The build copies `ffmpeg` and `ffprobe` (and on Windows their DLLs) next to the executable. The bundled ffmpeg is LGPL only (`packaging/check-ffmpeg-license.sh <ffmpeg>` checks it). On Linux, Mango can also use the system ffmpeg, or build without the bundled one (`MANGO_BUNDLE_FFMPEG=OFF`, Linux only); see [LINUX.md](LINUX.md). Without any ffmpeg, Mango still starts and says so (the `ffmpegMissing` notice), and streams fail with `ffmpegMissing`.

Remote streaming needs a signaling server (`signal/`). Pass its address at configure time, `-DMANGO_SIGNALING_URL=wss://signal.example.com/v1`, or set `remoteSignalingUrl` in settings.json, which wins. Without either, remote streaming stays disconnected.

## Tests

Off by default:

```sh
cmake --preset macos-clang -DMANGO_BUILD_TESTS=ON
cmake --build --preset macos
ctest --test-dir build
```

The live remote tests also run when `MANGO_LIVE_SIGNALING` holds a signaling server's address.

## Console server

`Mango` is the engine with terminal commands, for a machine without the app (SSH, a headless server) or for scripting.

```
mango> add-folder ~/Movies
mango> ls
mango> devices
mango> use 1
mango> play 3 sub=2
```

`stream <movie>` starts a stream and prints its URL, for VLC or any other player. `settings` and `set` change the transcoding quality, mode, listen mode and debug logging (`set debuglog on`); `languages` shows and sets the track language preferences (`languages audio jpn,eng`, `languages subtitles foreign|always|forced|off`, `languages subtitle-languages eng`); `help` lists every command. `play` and `stream` choose only the tracks given (`audio=N`, `sub=N|off`): the engine picks the others. When stdin closes (a piped script, or running as a service), it keeps serving until stopped.

By default the HTTP port is open only while something streams (`set listen always` keeps it open), and only on the LAN address.

## Linux package and service

See [LINUX.md](LINUX.md).
