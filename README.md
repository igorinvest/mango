# Mango Streaming Server

A lightweight macOS app that turns your Mac into a Chromecast/AirPlay (airplay is still in development and not available yet) streaming server for your local video library. Pick a video file, and Mango serves it (transcoding on the fly when needed)
to a browser-based control center you can cast from to any Chromecast device on your network.

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
