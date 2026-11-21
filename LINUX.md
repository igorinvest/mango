# Mango on Linux

On Linux, Mango runs headless: there's no desktop app, only the `mango` command. It runs in two ways, as the same program:

- **As a background service**: systemd runs Mango with no prompt, so it keeps serving after you log out.
- **In a terminal** (over SSH, too): `mango` shows a `mango>` prompt where you type commands to add videos, pick a TV and play. While the service runs, the terminal connects to it and the commands run there.

 It streams your videos to Chromecast devices on the same network, and gives you stream URLs for VLC or any other player.

It needs Ubuntu 24.04 or later, or Debian 13 or later, on x64.

## Install

```sh
curl -fsSL https://raw.githubusercontent.com/igorinvest/mango/main/packaging/linux/install.sh | sh
```

This downloads the newest `.deb` from [Releases](https://github.com/igorinvest/mango/releases), installs it with apt and starts Mango in the background. It asks for your password. Nothing is built on your machine.

Options:

```sh
curl -fsSL …/install.sh | MANGO_VERSION=v1.0.0 sh         # a specific release
curl -fsSL …/install.sh | MANGO_SYSTEM_FFMPEG=1 sh         # use the system ffmpeg instead of the bundled one
```

To install by hand instead, download `mango_amd64.deb` from Releases and run `sudo apt install ./mango_amd64.deb`.

To uninstall, run `sudo apt remove mango`. Your library and settings stay in `~/.local/share/Mango`. Delete that folder too to remove everything.

## First run

```
$ mango
mango> add-folder ~/Videos
mango> ls
mango> devices
mango> use 1
mango> play 3
```

- `help` lists every command.
- `play 3 sub=2` picks subtitle track 2. `info 3` lists a video's tracks.
- `stream <movie>` starts a stream and prints its URL, so you can open it in a player instead of a TV.
- `settings` and `set` change the transcoding quality, the transcoding mode, the listen mode and debug logging.
- `languages` sets your preferred audio and subtitle languages.
- `quit` exits. While the service runs, it only closes the prompt (see below).

Mango's data (the library, settings.json, pairing information and stream logs) is stored in `~/.local/share/Mango`. The settings are described in the README.

## Run in the background

The install script also starts Mango as a systemd user service. The service runs as you, so it can read your videos. It keeps running while you're logged out and starts again when the machine boots. To install without the service, use `MANGO_NO_SERVICE=1`.

**What it's for.** The service has no prompt, so it doesn't start playback by itself. It keeps your library available to the Mango apps you've paired with this machine (`pair` at the `mango>` prompt). Those apps can then browse it and play from it at any time. To play on a TV from Linux itself, use `mango` in a terminal.

**Using `mango` in a terminal while the service runs.** `mango` connects to the service ("Connected to the Mango service") and gives you the usual prompt, but every command runs in the service. So:

- `quit` or Ctrl+C closes the prompt and leaves the service running. A video you started keeps playing, and a folder keeps autoplaying.
- What the service reports on its own, such as "Playing next" or a pairing request, shows up in every terminal that's connected. Several terminals or SSH sessions can be connected at once.
- `shutdown` stops the service. It stays stopped until `systemctl --user start mango` or the next boot.
- `add-folder ~/Videos` and other paths are read in the terminal you type them in, so `~` and relative paths work as usual.

Only one Mango runs the engine at a time, because two would write the same library and settings in `~/.local/share/Mango`. Without the service, the first `mango` in a terminal runs it, and later terminals connect to that one; quitting the first stops Mango, and the others say "Mango stopped." A second engine that can't take the lock exits with "Mango is already running."

The connection is a Unix socket in `$XDG_RUNTIME_DIR/mango/`, which only your user can open. Nothing new listens on the network.

```sh
journalctl --user -u mango -f                # the service's output
systemctl --user disable --now mango         # stop it and don't start it at boot
```

If you installed the `.deb` by hand, turn the service on with:

```sh
systemctl --user enable --now mango
sudo loginctl enable-linger $USER
```

## Video conversion and GPUs

Mango converts a video when the TV can't play it as it is. The bundled ffmpeg can use the GPU for this through VAAPI (Intel, AMD) or NVENC and QSV. Install the driver for your GPU:

| GPU | Package |
|---|---|
| Intel | `intel-media-va-driver` |
| AMD | `mesa-va-drivers` |
| NVIDIA | the NVIDIA proprietary driver |

On a machine nobody logs into, also add yourself to the `render` group (`sudo usermod -aG render $USER`), then log out and back in.

Without a GPU, Mango converts with the CPU, which needs the system ffmpeg: `sudo apt install ffmpeg`. The bundled ffmpeg doesn't include a software H.264 encoder.

## Network

- **Chromecast discovery** uses mDNS (UDP port 5353).
- **Streaming** uses an HTTP server on port 8080, or on the next free port if 8080 is taken. Typing `server` at the `mango>` prompt shows the address. By default the port is open only while something streams; `set listen always` keeps it open.

If you use a firewall, allow both from your local network. With ufw, for example:

```sh
sudo ufw allow from 192.168.1.0/24 to any port 5353 proto udp
sudo ufw allow from 192.168.1.0/24 to any port 8080:8090 proto tcp
```

## WSL

Mango runs under WSL 2, with two changes on the Windows side.

**Mirrored networking**, so the Chromecast and Mango see each other. By default WSL puts Linux behind NAT on a separate network. In `%UserProfile%\.wslconfig`:

```ini
[wsl]
networkingMode=mirrored

[experimental]
hostAddressLoopback=true
```

Then run `wsl --shutdown` in PowerShell and open WSL again.

**Windows firewall.** Allow WSL's inbound traffic. In PowerShell as administrator:

```powershell
Set-NetFirewallHyperVVMSetting -Name '{40E0AC32-46A5-438A-A0B2-2B479E8F2E90}' -DefaultInboundAction Allow
```

The service needs systemd, which current Ubuntu and Debian WSL images have on by default. If `systemctl --user` fails, add `systemd=true` under `[boot]` in `/etc/wsl.conf` and restart WSL. WSL doesn't give the bundled ffmpeg a GPU, so install the system ffmpeg (`sudo apt install ffmpeg`).

## Troubleshooting

- **No Chromecast in `devices`**: Mango and the Chromecast must be on the same network, and mDNS must get through the firewall. Under WSL, check mirrored networking.
- **The TV starts playing but stalls or fails**: the TV can't reach the HTTP port. Check the firewall, and look at the address `server` shows.
- **Conversion fails or is slow**: check the GPU driver and the `render` group, or install the system ffmpeg. `set debuglog on`, then restart Mango, for more detail in the log.

## Build from source

You only need this to change the code. The release packages are built the same way, by `.github/workflows/linux.yml` on Ubuntu 24.04.

**Tools:**

```sh
wget https://apt.llvm.org/llvm.sh && sudo bash llvm.sh 22      # Clang 22
sudo apt install cmake git curl zip unzip tar libstdc++-14-dev nasm autoconf automake \
    autoconf-archive libtool pkg-config python3-venv libdrm-dev dpkg-dev
git clone https://github.com/microsoft/vcpkg.git ~/vcpkg && ~/vcpkg/bootstrap-vcpkg.sh -disableMetrics
export VCPKG_ROOT="$HOME/vcpkg"                                # add this to ~/.bashrc
```

CMake must be 3.25 or later. Clone vcpkg with its full history, because `vcpkg.json` pins the dependency versions to a vcpkg commit.

**Build, run and test:**

```sh
cmake --preset linux-x64 -DMANGO_BUILD_TESTS=ON
cmake --build --preset linux-x64-debug -j "$(nproc)"
./build-linux-x64/Mango
ctest --test-dir build-linux-x64
```

The first build takes tens of minutes, because vcpkg compiles ffmpeg and the other dependencies. After that, a rebuild only compiles what changed.

**Package:**

```sh
cmake --preset linux-x64-release
cmake --build --preset linux-x64-release -j "$(nproc)"
cd build-linux-x64-release && cpack          # mango_<version>_amd64.deb and a .tar.gz
```

The package installs `Mango` and the bundled ffmpeg in `/usr/lib/mango`, the `mango` command in `/usr/bin`, and the user service in `/usr/lib/systemd/user`. It requires GCC 14's C++ runtime (`libstdc++6` 14 or later). To build a package for older distributions such as Debian 12 or Ubuntu 22.04, add `-DMANGO_STATIC_CXX_RUNTIME=ON` when you configure. This links the runtime into `Mango` and adds a few MB.

The `linux-x64-system-ffmpeg` preset doesn't build ffmpeg at all (`MANGO_BUNDLE_FFMPEG=OFF`), so it builds faster. Its package depends on the system `ffmpeg` package. Mango finds that ffmpeg in `PATH`, or in the folder set as `ffmpegFolder` in settings.json. The bundled ffmpeg is LGPL only; `packaging/check-ffmpeg-license.sh <path-to-ffmpeg>` checks this.
