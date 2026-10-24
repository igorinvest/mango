# Mango Streaming Server

A lightweight native app for macOS and Windows that turns your computer into a Chromecast/AirPlay
streaming server for your local video library. Pick a video file in the app, and Mango serves it
(transcoding on the fly when needed) to any Chromecast device on your network.

<img width="512" height="336" alt="Windows screenshot" src="https://github.com/user-attachments/assets/29a60d9f-9390-4df5-9b1e-6f8127dfc4ca" />
<img width="526" height="382" alt="MacOS screenshot" src="https://github.com/user-attachments/assets/7e9eea11-d413-4c2e-89ef-e05f611930eb" />

## Why Mango? A lightweight alternative to Plex and Jellyfin

If you just want to cast a movie file from your Mac or PC to a Chromecast without running a full media
server platform, Mango is a lightweight alternative to Plex and Jellyfin:

- **Zero setup** — no Docker containers, no metadata agents to configure. Download it, pick a
  file, and cast it. Want a library? Add a folder and Mango finds every video in it — but you
  never have to.
- **Native TV playback** — Mango casts straight into Chromecast's own built-in default player, so
  your TV shows a standard, native Cast experience instead of a custom branded receiver app.
- **Lightweight and fast** — a small native C++ engine instead of the heavier server processes
  behind Plex and Jellyfin, so it starts instantly and stays out of the way.
- **A real native macOS/Windows app** — not a self-hosted server you have to babysit. Windows
  builds ship through the Microsoft Store with automatic updates; Mac App Store distribution is
  coming soon.

Mango isn't trying to replace everything Plex or Jellyfin offer — their metadata libraries,
multi-user accounts, and remote access are a different, bigger scope. Mango is built for a
narrower job: casting a video file to your TV as simply and reliably as possible, as a
lightweight alternative for anyone who doesn't need a full media server just to watch one movie.

## Download

Get Mango for Windows from the
[Microsoft Store](https://apps.microsoft.com/detail/9NRNZQHLVB5F), or grab the latest build for
either platform from the [Releases](../../releases) page.

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

The easiest way (and the latest version) is the [Microsoft Store](https://apps.microsoft.com/detail/9NRNZQHLVB5F), which
installs and updates Mango for you.

To install from Releases instead, download `MangoStreamingServer.cer` and the `.msix` package
from the release, then:

1. Right-click `MangoStreamingServer.cer` → **Install Certificate** → **Local Machine** → place it
   in the **Trusted People** store. This isn't signed by a public certificate authority, so
   Windows needs to be told to trust it first — the equivalent of the macOS Gatekeeper step above.
   You only need to do this once.
2. Double-click the `.msix` file → **Install**.

## Requirements

**macOS**

- macOS 15.7 or later
- A Chromecast (or other Google Cast-compatible) device on the **same Wi-Fi network** as your Mac

**Windows**

- Windows 10 version 1809 (build 17763) or later
- A Chromecast (or other Google Cast-compatible) device on the **same Wi-Fi network** as your PC

## Usage

1. Launch **Mango Streaming Server** — it starts the local server automatically, and the sidebar
   shows your Chromecast (and, on macOS, AirPlay) devices as they're discovered on the network.
2. Click **+** in the Media Library toolbar and pick a video file. Mango reads its media info and
   adds it to your library — no separate web page to visit. Or click **Add Folder** to add a whole
   folder: Mango scans it in the background and adds every video it finds, including those in
   subfolders. **Refresh** re-scans your folders to pick up new files.
3. Pick a device in the sidebar, then click a movie's **Play** button to cast it with its
   recommended tracks, or use the card's audio/subtitle menu to pick specific tracks first.
   Playback controls (play/pause, stop, seek, and switching tracks mid-stream) live in the docked
   bar at the bottom of the window.
4. Mango remembers where you left off — playing a movie again resumes from your last position
   (unless you've already finished it, in which case it starts over from the beginning).

## Transcoding settings

The sidebar's transcode mode covers most cases:

- **Automatic** copies the video as-is when your TV can play it, and converts it otherwise.
- **Force Decode** always converts the video, for maximum compatibility.
- **Force Copy** never converts the video. It's the fastest option, but only works if your TV
  supports the file's format.

For finer control, Mango keeps its configuration in JSON files that you can edit by hand. Quit
Mango before editing them: the files are only read at startup, and changing a setting in the app
rewrites `settings.json`, which would undo your edits.

**Where the files are**

- **macOS:** `~/Library/Containers/<Mango's container>/Data/Library/Application Support/Mango/`.
  In Finder, the container folder is listed as **Mango Streaming Server**.
- **Windows (Microsoft Store or `.msix`):**
  `%LOCALAPPDATA%\Packages\<Mango's package>\LocalCache\Roaming\Mango\`. The package folder
  is the one whose name contains `MangoStreamingServer`.

If a file is missing, Mango recreates it with the defaults the next time it starts. To undo
your changes, delete the file. Keep the JSON valid and the key names exact: if Mango can't read
`stream-profiles.json` or `encoding-defaults.json` (because of a syntax error, a misspelled key
or an unknown key), it replaces the file with the defaults.

**`encoding-defaults.json`: quality of converted videos**

These settings only apply to tracks Mango has to convert. Tracks it copies keep their
original quality.

```json
{"videoBitrate": "5000k", "videoPixFmt": "yuv420p", "audioCodec": "aac", "audioBitrate": "256k"}
```

- `videoBitrate`: raise it for better picture quality (e.g. `"8000k"` for 1080p, `"15000k"` for
  4K), or lower it if playback stutters on slow Wi-Fi.
- `audioBitrate`: the audio quality of converted tracks.
- `videoPixFmt` and `audioCodec`: leave these as they are unless you know your TV needs
  something else.

**`stream-profiles.json`: what your TV can play without conversion**

This file decides what Mango copies and what it converts. It has one profile per protocol:
`dash` for Chromecast and `hls` for AirPlay. A track is copied when it matches one of the
profile's entries. Otherwise it's converted, using `encoding-defaults.json`.

- `videoCapabilities`: video codecs the TV plays (`h264`, `hevc`, `vp9`, …), each with
  `maxWidth`, `maxHeight` and `maxFps`. Converted video keeps its original resolution, so
  lowering these limits doesn't make 4K files play on an HD-only device. Remove a codec your TV
  can't play (for example `vp9`) so those files get converted to H.264 instead of copied.
- `audioCapabilities`: audio codecs the TV plays (`aac`, `opus`, …), each with `maxChannels`
  and `maxSampleRate`.
- `segmentDurationSec`: the length of each streamed chunk in seconds.

Leave the other fields (file names, extensions, subtitle settings) unchanged.

**`settings.json`: general settings**

- `videoTranscodeMode`: `automatic`, `forceDecode` or `forceCopy` (the same as the sidebar
  setting).
- `defaultAudioLanguage` and `defaultSubtitlesLanguage`: preferred track languages, as
  three-letter codes (e.g. `"eng"`).
- `streamStartDelayMs`: how long Mango waits after starting the conversion before telling the
  TV to play (in milliseconds, up to `30000`). Raise it if playback fails or stalls right after
  starting.
- `streamStartReadyPercent`: the wait ends early once this percentage of the file has been
  converted (`0` to `100`).
