# Mango Streaming Server

A lightweight native app for macOS and Windows that turns your computer into a Chromecast/AirPlay
streaming server for your local video library. Pick a video file in the app, and Mango serves it
(transcoding on the fly when needed) to any Chromecast device on your network.

## Why Mango? A lightweight alternative to Plex and Jellyfin

If you just want to cast a movie file from your Mac to a Chromecast without running a full media
server platform, Mango is a lightweight alternative to Plex and Jellyfin:

- **Zero setup** — no Docker containers, no library scans, no metadata agents to configure.
  Download it, pick a file, and cast it.
- **Native TV playback** — Mango casts straight into Chromecast's own built-in default player, so
  your TV shows a standard, native Cast experience instead of a custom branded receiver app.
- **Lightweight and fast** — a small native C++ engine instead of the heavier server processes
  behind Plex and Jellyfin, so it starts instantly and stays out of the way.
- **A real native macOS app** — not a self-hosted server you have to babysit. Mac App Store
  distribution with automatic updates is coming soon; for now, grab it from Releases below.

Mango isn't trying to replace everything Plex or Jellyfin offer — their metadata libraries,
multi-user accounts, and remote access are a different, bigger scope. Mango is built for a
narrower job: casting a video file to your TV as simply and reliably as possible, as a
lightweight alternative for anyone who doesn't need a full media server just to watch one movie.

## Download

Grab the latest build from the [Releases](../../releases) page.

### macOS

Unzip it and drag **Mango Streaming Server.app** to your Applications folder.

This build isn't notarized by Apple, so macOS Gatekeeper will warn that it's from an
"unidentified developer" the first time you open it. This is expected — pick **one** of the
following:

- Right-click (or Control-click) the app → **Open** → confirm **Open** in the dialog.
- Or open **System Settings → Privacy & Security**, scroll down, and click **Open Anyway** next
  to the Mango Streaming Server entry.
- Or, in Terminal: `xattr -cr "/Applications/Mango Streaming Server.app"`

You only need to do this once.

### Windows

Microsoft Store distribution is coming soon; for now, grab it from Releases below.

Download `MangoStreamingServer.cer` and the `.msix` package from the release, then:

1. Right-click `MangoStreamingServer.cer` → **Install Certificate** → **Local Machine** → place it
   in the **Trusted People** store. This isn't signed by a public certificate authority, so
   Windows needs to be told to trust it first — the equivalent of the macOS Gatekeeper step above.
   You only need to do this once.
2. Double-click the `.msix` file → **Install**.

## Requirements

**macOS**

- macOS 15.7 or later
- A Chromecast (or other Google Cast-compatible) device on the **same Wi-Fi network** as your Mac
- Google Chrome or Microsoft Edge for casting — some Chromium-based browsers (Brave, plain
  Chromium builds) omit real Google Cast support

**Windows**

- Windows 10 version 1809 (build 17763) or later
- A Chromecast (or other Google Cast-compatible) device on the **same Wi-Fi network** as your PC
- Google Chrome or Microsoft Edge for casting — some Chromium-based browsers (Brave, plain
  Chromium builds) omit real Google Cast support

## Usage

1. Launch **Mango Streaming Server** — it starts the local server automatically, and the sidebar
   shows your Chromecast (and, on macOS, AirPlay) devices as they're discovered on the network.
2. Click **+** in the Media Library toolbar and pick a video file. Mango reads its media info and
   adds it to your library — no separate web page to visit.
3. Pick a device in the sidebar, then click a movie's **Play** button to cast it with its
   recommended tracks, or use the card's audio/subtitle menu to pick specific tracks first.
   Playback controls (play/pause, stop, seek, and switching tracks mid-stream) live in the docked
   bar at the bottom of the window.
4. Mango remembers where you left off — playing a movie again resumes from your last position
   (unless you've already finished it, in which case it starts over from the beginning).

## License

Mango bundles `ffmpeg`/`ffprobe` binaries built under the LGPL license (no GPL-only components
enabled). See the [FFmpeg project](https://ffmpeg.org/legal.html) for details.
