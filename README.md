# Mango Streaming Server

A lightweight macOS app that turns your Mac into a Chromecast/AirPlay (airplay is still in development and not available yet) streaming server for your local video library. Pick a video file, and Mango serves it (transcoding on the fly when needed) to a browser-based control center you can cast from to any Chromecast device on your network.

## Why Mango? A lightweight alternative to Plex and Jellyfin

If you just want to cast a movie file from your Mac to a Chromecast without running a full media server platform, Mango is a lightweight alternative to Plex and Jellyfin:

- **Zero setup** — no Docker containers, no library scans, no metadata agents to configure. Download it, pick a file, and cast it.
- **Native TV playback** — Mango casts straight into Chromecast's own built-in default player, so your TV shows a standard, native Cast experience instead of a custom branded receiver app.
- **Lightweight and fast** — a small native C++ engine instead of the heavier server processes behind Plex and Jellyfin, so it starts instantly and stays out of the way.
- **Built-in Chromecast troubleshooting** — a live-checking diagnostic panel for the most common casting problems (no Cast device found, unsupported browser, local network permissions), which neither Plex nor Jellyfin surfaces as directly.
- **A real native macOS app** — not a self-hosted server you have to babysit. Mac App Store distribution with automatic updates is coming soon; for now, grab it from Releases below.

Mango isn't trying to replace everything Plex or Jellyfin offer. Mango is for casting a video file to your TV as simply and reliably as possible. It is a lightweight alternative for anyone who doesn't need a full media server just to watch one movie.

## Download

Grab the latest build from the [Releases](../../releases) page, unzip it, and drag
**Mango Streaming Server.app** to your Applications folder.

### First launch (important)

This build isn't notarized by Apple, so macOS Gatekeeper will warn that it's from an
"unidentified developer" the first time you open it. This is expected — pick **one** of the
following:

- Right-click (or Control-click) the app → **Open** → confirm **Open** in the dialog.
- Or open **System Settings → Privacy & Security**, scroll down, and click **Open Anyway** next
  to the Mango Streaming Server entry.
- Or, in Terminal: `xattr -cr "/Applications/Mango Streaming Server.app"`

You only need to do this once.

## Requirements

- macOS 15.7 or later
- A Chromecast (or other Google Cast-compatible) device on the **same Wi-Fi network** as your Mac
- Google Chrome or Microsoft Edge for casting — some Chromium-based browsers (Brave, plain
  Chromium builds) omit real Google Cast support

## Usage

1. Launch **Mango Streaming Server** — it starts the local server automatically.
2. Click **+ Add Movie** in the web UI (opened via the app, or the address shown in the app
   window) and pick a video file. Mango reads its media info and adds it to your library.
3. Select a movie, choose audio/subtitle tracks, and hit **Play** to cast it to your TV.
4. If something doesn't work, click **Video not working?** in the web UI for a built-in
   troubleshooting checklist (it live-checks a few of the most common causes for you).

The native app window also shows the server's local and network addresses (with one-click copy)
and a shortcut to launch a Chromium-based browser straight to the control center.

## License

Mango bundles `ffmpeg`/`ffprobe` binaries built under the LGPL license (no GPL-only components
enabled). See the [FFmpeg project](https://ffmpeg.org/legal.html) for details.
