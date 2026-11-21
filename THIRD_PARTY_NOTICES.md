# Third-party notices

Mango's own code is under the [PolyForm Noncommercial License 1.0.0](LICENSE.md). The components below are not; each stays under its own license.

## Fetched by vcpkg at build time

Nothing third-party is vendored in this repository; every dependency comes from vcpkg.

| Component | License |
|---|---|
| [FFmpeg](https://ffmpeg.org) (`ffmpeg`, `ffprobe`, bundled next to the executable) | LGPL 2.1 or later, as built here (no GPL-only or non-free components enabled). See <https://ffmpeg.org/legal.html>. |
| [glaze](https://github.com/stephenberry/glaze) | MIT |
| [{fmt}](https://github.com/fmtlib/fmt) | MIT |
| [BLAKE3](https://github.com/BLAKE3-team/BLAKE3) | CC0 1.0 or Apache 2.0 |
| [mdns](https://github.com/mjansson/mdns) | Unlicense (public domain) |
| [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) | Apache 2.0 |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | MIT |
| [libdatachannel](https://github.com/paullouisageneau/libdatachannel) | MPL 2.0 |
| [libjuice](https://github.com/paullouisageneau/libjuice) (used by libdatachannel) | MPL 2.0 |
| [usrsctp](https://github.com/sctplab/usrsctp) (used by libdatachannel) | BSD 3-Clause |
| [plog](https://github.com/SergiusTheBest/plog) (used by libdatachannel) | MIT |
| [nlohmann/json](https://github.com/nlohmann/json) (used by libdatachannel) | MIT |
| [OpenSSL](https://www.openssl.org) | Apache 2.0 |

The bundled ffmpeg also includes the hardware encoder interfaces enabled for each platform: [libva](https://github.com/intel/libva) (Linux, MIT), [nv-codec-headers](https://github.com/FFmpeg/nv-codec-headers) (Windows and Linux, MIT), [libvpl](https://github.com/intel/libvpl) (Windows and Linux, MIT) and [AMF](https://github.com/GPUOpen-LibrariesAndSDKs/AMF) (Windows, MIT). GPU drivers are not bundled; they come from the system.

vcpkg installs each package's license file under `vcpkg_installed/<triplet>/share/<package>/copyright`. Anyone redistributing a build must include those files.
