# Mango Streaming Server

A lightweight native app for macOS and Windows that turns your computer into a Chromecast/AirPlay
streaming server for your local video library. Pick a video file in the app, and Mango serves it
(transcoding on the fly when needed) to any Chromecast device on your network. Pair two Mangos and
each can play the other's library on its own TV, even over the internet: see
[Remote streaming](#remote-streaming).

On Linux, Mango runs headless, as a console server or a systemd service: see [LINUX.md](LINUX.md).

<img width="512" height="336" alt="Windows screenshot" src="https://github.com/user-attachments/assets/29a60d9f-9390-4df5-9b1e-6f8127dfc4ca" />
<img width="526" height="382" alt="MacOS screenshot" src="https://github.com/user-attachments/assets/7e9eea11-d413-4c2e-89ef-e05f611930eb" />

## Why Mango? A lightweight alternative to Plex and Jellyfin

If you just want to cast a movie file from your Mac or PC to a Chromecast without running a full media
server platform, Mango is a lightweight alternative to Plex and Jellyfin:

- **Zero setup** — no Docker containers, no metadata agents to configure. Download it, pick a
  file, and cast it. Want a library? Add a folder and Mango finds every video in it — but you
  never have to.
- **No account, no telemetry** — no sign-up, no login, no cloud service in between. Mango
  doesn't collect usage data or phone home: your library and what you watch stay on your
  computer and your local network. The only exception is remote streaming, which is off until
  you turn it on (see [Remote streaming](#remote-streaming)).
- **Your library on another TV** — pair two Mangos with a QR code or link, and one plays the
  other's videos on its own TV, at home or across the internet. No account, no port forwarding,
  no certificates to set up.
- **Native TV playback** — Mango casts straight into Chromecast's own built-in default player, so
  your TV shows a standard, native Cast experience instead of a custom branded receiver app.
- **Multitrack audio and subtitles** — pick any audio or subtitle track built into the file, or
  an external one sitting next to it (a separate dub `.mka`, an `.srt` in a `Subs` folder), and
  switch tracks mid-stream. Your preferred languages are picked automatically.
- **Folders as playlists** — add a folder and play any video in it: when it ends, Mango plays
  the next one in the folder on the same TV, so a season of episodes plays through on its own.
- **Lightweight and fast** — a small native C++ engine instead of the heavier server processes
  behind Plex and Jellyfin, so it starts instantly and stays out of the way.
- **A real native macOS/Windows app** — not a self-hosted server you have to babysit. Windows
  builds ship through the Microsoft Store with automatic updates; Mac App Store distribution is
  coming soon.

Mango isn't trying to replace everything Plex or Jellyfin offer — their metadata libraries,
multi-user accounts, and streaming to browsers and phones are a different, bigger scope. Mango is built for a
narrower job: casting a video file to your TV as simply and reliably as possible, as a
lightweight alternative for anyone who doesn't need a full media server just to watch one movie.

## Download

Get Mango for Windows from the
[Microsoft Store](https://apps.microsoft.com/detail/9NRNZQHLVB5F), or grab the latest build for
either platform from the [Releases](https://github.com/igorinvest/mango/releases) page.

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

**Linux** (headless, no desktop app)

- Ubuntu 24.04 or later, or Debian 13 or later, x64. Install with one command: see [LINUX.md](LINUX.md).

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
5. Playing a video from a folder plays the rest of that folder after it — see
   [Playing a folder](#playing-a-folder).

## Supported formats

- **Video files:** `.mkv`, `.mp4`, `.mov`, `.avi`, `.webm`, `.m2ts`, `.ts`. These are the files
  **+** offers and **Add Folder** picks up.
- **Video and audio codecs:** anything ffmpeg can decode — H.264, H.265 (HEVC), VP9, MPEG-2,
  MPEG-4 and more; AAC, AC-3, E-AC-3, DTS, FLAC, Opus, MP3 and more. What your TV plays is
  streamed as-is; everything else is converted on the fly (see
  [Transcoding settings](#transcoding-settings)).
- **Subtitles:** text tracks built into the file (SRT, ASS/SSA, WebVTT, mov_text) and external
  `.srt`, `.vtt`, `.ass` and `.ssa` files are converted to WebVTT for the TV. Image-based
  subtitles (Blu-ray PGS, DVD and DVB) can't be shown and are left out of the subtitle menu.
- **External audio:** `.mka`, `.ac3`, `.eac3`, `.dts`, `.aac`, `.m4a`, `.flac`, `.mp3`, `.opus`
  (see below).

## External subtitles and audio

Subtitle and audio files that sit next to a video show up in its audio/subtitle menu, after the
tracks built into the file. Picking one works like picking a built-in track, on Chromecast and
AirPlay, before playback or mid-stream.

- **Subtitles:** `.srt`, `.vtt`, `.ass`, `.ssa`. Files that aren't UTF-8 (for example older
  Cyrillic or Central European subtitles) are converted automatically.
- **Audio:** `.mka`, `.ac3`, `.eac3`, `.dts`, `.aac`, `.m4a`, `.flac`, `.mp3`, `.opus` — typically
  a dub shipped as a separate file.

**Which files are found.** For a video named `Movie.mkv`, the same rules apply to subtitles and
audio:

- **Named after the video**, next to it or in any folder up to 3 levels below it:
  `Movie.srt`, `Movie.en.forced.srt`, `Movie.rus.ac3`, `Extras/Audio/Movie.mka`. The name must
  start with the video's name followed by `.`, `_` or `-` — `Movie 2.srt` belongs to
  `Movie 2.mkv`, not `Movie.mkv`.
- **Any file in a subtitles or audio folder** next to the video, and in that folder's subfolders,
  when none there is named after the video (a single movie's release):
  - subtitles: `Subs`, `Subtitles`, `<anything> Subs` (`RUS Subs`), `<anything> Subtitles`;
  - audio: `Audio`, `Sound`, `Sounds`, `<anything> Sound` (`Rus Sound`), `<anything> Sounds`;
  - a folder named after the video counts too: `Subs/Movie/2_English.srt`.

  When a folder holds files for several episodes, each episode only gets the files named after
  it.
- **Another episode's folder is skipped:** `Subs/S01E02/` is never searched for `S01E01.mkv`.

**Language and tags** are read from the file name: `Movie.en.srt`, `Movie.English.srt`,
`Movie.rus.ac3`. When the name has no language, it's taken from the folders the file is in, so
`RUS Subs/[Studio]/Movie.Studio.ass` and `Rus Sound/LostFilm/Movie.mka` are Russian. `forced`,
`sdh`/`hi`/`cc` and `commentary` in the name are recognized too. A file with no recognizable
language is still listed (by its file name); it just isn't picked automatically by language.

**Default tracks.** The preferred audio and subtitle languages apply across built-in and
external tracks alike: if you usually watch with Russian audio, a Russian `.mka` is picked even
when the video has no built-in Russian track. When both have the same language, the built-in
track wins.

**Limits:**

- External files are only found for videos added through **Add Folder**. Videos added one at a
  time with **+** use only the tracks built into the file.
- New or deleted files are picked up on **Refresh** and at launch.

## Playing a folder

Playing a video from a folder section turns that section into a playlist. When the video ends,
the Now Playing bar shows **Playing next in 5…** with the next video's title, and then plays it
on the same device. **Cancel** stops the chain; **Now** skips the countdown. During playback,
the bar shows **Up next** with a **Next** button to skip ahead by hand.

- **The order is the grid's current sort.** With Name sort, numbers are compared by value, so
  `Episode 2` comes before `Episode 10`. Changing the sort during playback changes what comes
  next. The search box doesn't filter the playlist.
- **Only the videos directly in that folder** are played. At the end of the folder playback
  stops; it doesn't continue into subfolders or the next season's folder. Videos in **Added
  Manually** have no playlist.
- **Each video starts like it would from its Play button:** it resumes if it was half-watched
  (otherwise from the beginning), with your preferred audio and subtitle languages.
- **Only a real ending starts the next video.** Stopping playback, the TV switching to another
  app, or a conversion that failed partway never does.
- **Play next video in folder automatically** in the sidebar turns this off (it's on by
  default). With it off, Up next and Next are hidden too.
- The Mango app has to stay open for the next video to start.

## Remote streaming

Remote streaming lets two Mangos use each other's libraries, on the same network or over the
internet. For example, play the movies on your home computer on the TV at a holiday house,
through a laptop there running Mango.

- A Mango you pair with becomes your **client**: it lists your library and plays your videos on
  its own TV. In the other direction, a Mango whose library you play is your **source**. The
  same two devices can be each other's client and source.
- Remote streaming is in **Settings → Remote**. It's off by default.

**Pairing**

1. On the Mango that has the videos, click **Show Pairing Code** under **My clients**. It shows a
   QR code and a link that work once and expire after 10 minutes.
2. On the other Mango, paste the link under **My sources** and click **Pair**.

Both Mangos must be running while they pair. Afterwards, they find each other again on their
own at every launch.

**Playing**

A source's videos show up in the Media Library under its name, next to your own. Play them like
any other video: pick a TV in the sidebar and click **Play**. **Refresh
Library** loads the source's library again. While a source is offline its videos stay listed,
marked offline.

**Good to know**

- Both computers must stay on, awake and running Mango during playback. The source does the
  conversion, so its transcoding settings and hardware matter, not the client's.
- **Allow my clients to stream** stops your clients from playing your library without
  unpairing them. **Remove** unpairs a device; to use it again, pair again.
- The devices find each other through Mango's signaling service, which only introduces them.
  Videos go directly from one Mango to the other, encrypted, and pairing is verified end to
  end, so the service can't read or change them.
- If both networks block direct connections (some mobile and carrier-grade NAT networks do),
  the link fails with "Can't connect".
- On Linux, remote streaming works from the `mango` command and the service: see
  [LINUX.md](LINUX.md#remote-streaming). A Linux machine running the service is a handy
  always-on source.

## Transcoding settings

The sidebar's transcode mode covers most cases:

- **Automatic** copies the video as-is when your TV can play it, and converts it otherwise.
- **Force Decode** always converts the video, for maximum compatibility.
- **Force Copy** never converts the video. It's the fastest option, but only works if your TV
  supports the file's format.

The rest is in **Settings** (the gear next to the status in the sidebar, or ⌘, on macOS and
Ctrl+, on Windows). Every setting there applies right away.

**Target Quality** (720p, 1080p or 4K, in Settings → Video) caps the size of the video
Mango streams. Converted video is scaled down to fit it, never up, and a file larger than the
target is converted even if your TV could play it as-is. Lower it if playback stutters on weak
Wi-Fi. Force Copy ignores it.

**Supported Formats** (in Settings → Video) tells Mango what your TV plays, so Automatic knows
what it can copy:

- **Video:** for each codec, the highest resolution your TV plays it at, or **Not supported**
  to always convert files in that codec. Older Chromecasts (before Chromecast with Google TV)
  can't play H.265 (HEVC): set it to Not supported for them.
- **Rotated video:** tick it if your TV shows phone recordings upright when they're copied.
  Unticked, rotated videos are always converted, which fixes their orientation.
- **Audio:** ticked codecs are copied, unticked ones are converted to AAC.

On macOS there's a tab for Chromecast and one for AirPlay. Changes apply to the next video you
play.

**Languages** (in Settings) sets your preferred audio and subtitle languages and when subtitles
show. **Advanced** turns on debug logging (from the next launch) and opens the logs and
settings folders.

For finer control, Mango keeps its configuration in JSON files that you can edit by hand.
`stream-profiles.json` and `encoding-defaults.json` are read every time a video starts, so edits
to them apply to the next video without a restart. `settings.json` is only read at startup: quit
Mango before editing it, since changing a setting in the app rewrites it and would undo your
edits.

**Where the files are**

- **macOS:** `~/Library/Containers/<Mango's container>/Data/Library/Application Support/Mango/`.
  In Finder, the container folder is listed as **Mango Streaming Server**.
- **Windows (Microsoft Store or `.msix`):**
  `%LOCALAPPDATA%\Packages\<Mango's package>\LocalCache\Roaming\Mango\`. The package folder
  is the one whose name contains `MangoStreamingServer`.
- **Linux:** `~/.local/share/Mango/` (or `$XDG_DATA_HOME/Mango/`).

Settings → Advanced → **Open Settings Folder** opens it.

If a file is missing, Mango recreates it with the defaults. To undo your changes, delete the
file (or use **Restore Defaults** in Supported Formats). Keep the JSON valid and the key names
exact: if Mango can't read `stream-profiles.json` or `encoding-defaults.json` (because of a
syntax error, a misspelled key or an unknown key), it renames it to
`stream-profiles.json.invalid` (or `encoding-defaults.json.invalid`), so your edits aren't lost,
and writes the defaults in its place. The same goes for `device-profiles.json`. Mango shows a
warning when it does this.

**`encoding-defaults.json`: quality of converted videos**

These settings only apply to tracks Mango has to convert. Tracks it copies keep their
original quality.

```json
{
  "presets": {
    "720p":  {"maxWidth": 1280, "maxHeight": 720,  "videoBitrate": "3000k",  "audioBitrate": "192k"},
    "1080p": {"maxWidth": 1920, "maxHeight": 1080, "videoBitrate": "6000k",  "audioBitrate": "256k"},
    "4k":    {"maxWidth": 3840, "maxHeight": 2160, "videoBitrate": "16000k", "audioBitrate": "256k"}
  },
  "videoPixFmt": "yuv420p",
  "audioCodec": "aac"
}
```

- `presets`: one entry per Target Quality. `maxWidth`/`maxHeight` is the largest video that
  quality allows; sizes are compared long side to long side, so a portrait phone video
  (1080×1920) counts as 1080p. The bitrates are picked by the size of the video Mango outputs,
  not by the Target Quality: a 720p file gets the `720p` bitrates even on the 4K setting. Raise
  `videoBitrate` for better picture quality, or lower it if playback stutters.
- `videoPixFmt` and `audioCodec`: leave these as they are unless you know your TV needs
  something else.

Files written by Mango versions before Target Quality used a different format. Mango keeps an
old file as `encoding-defaults.json.invalid` and writes the new defaults; copy your bitrates
over by hand if you'd changed them.

**`stream-profiles.json`: what your TV can play without conversion**

This is the file Supported Formats edits. It has one profile per protocol: `dash` for
Chromecast and `hls` for AirPlay. A video track is copied when it matches one of the profile's
entries and fits the Target Quality. Otherwise it's converted to H.264.

- `videoCapabilities`: video codecs the TV plays (`h264`, `hevc`, `vp9`, …), each with
  `maxWidth`, `maxHeight` and `maxFps`. A codec can have several entries (for example H.264 at
  4K up to 30 fps and at 1080p up to 60 fps); a video is copied if it fits any of them.
  Changing a codec in Supported Formats replaces its entries, including any fps limits you set
  by hand.
- `audioCapabilities`: audio codecs the TV plays (`aac`, `opus`, …), each with `maxChannels`
  and `maxSampleRate`.
- `rotationSupported`: the **Rotated video** checkbox (`false` if missing).
- `segmentDurationSec`: the length of each streamed chunk in seconds.

Leave the other fields (file names, extensions, subtitle settings) unchanged. A file written by
an earlier version stays valid, but keeps its old defaults: use **Restore Defaults** to pick up
H.265 (HEVC) support for Chromecast.

**`settings.json`: general settings**

- `videoTranscodeMode`: `automatic`, `forceDecode` or `forceCopy` (the same as the sidebar
  setting).
- `encodingPreset`: `720p`, `1080p` or `4k` (the Target Quality setting; `1080p` by default).
- `audioLanguages`: preferred audio languages, best first, as language codes (e.g.
  `["jpn", "eng"]`). Empty: the file's default track.
- `subtitleMode`: when subtitles show for a file you didn't pick tracks for: `off`,
  `foreignAudioOnly` (when the audio isn't in one of your languages; the default), `always` or
  `forcedOnly`. Forced subtitles in the audio's language show in every mode but `off`.
- `subtitleLanguages`: preferred subtitle languages, best first. Empty: the same as
  `audioLanguages`.

  Playback never changes these. Tracks you pick are remembered for the file, and their
  languages for the rest of its folder (`folder_tracks.json`).
- `autoplayNextInFolder`: the **Play next video in folder automatically** setting (`true` by
  default).
- `listenOnlyWhileStreaming`: opens the HTTP port only while something streams (`true` by
  default).
- `showSystemNotifications`: a notification when a video finishes while Mango's window isn't
  in front (`true` by default).
- `logDebug`: more detail in the logs, from the next launch (`false` by default).
- `streamStartDelayMs`: how long Mango waits after starting the conversion before telling the
  TV to play (in milliseconds, up to `30000`). Raise it if playback fails or stalls right after
  starting.
- `streamStartReadyPercent`: the wait ends early once this percentage of the file has been
  converted (`0` to `100`).
- `remoteStreamingEnabled`: the **Remote streaming** switch (`false` by default).
- `remoteStreamingAllowed`: **Allow my clients to stream** (`true` by default).
- `remoteDeviceName`: this device's name as paired Mangos see it, from the next pairing.
  Unset: the computer's name.
- `remoteSignalingUrl`: the signaling service. Release builds have it built in; builds from
  source need it set (see [LINUX.md](LINUX.md#the-signaling-server)).
- `ffmpegFolder` (Linux only): a folder with an `ffmpeg` and `ffprobe` to use when the
  bundled ones lack an encoder (see [LINUX.md](LINUX.md)).

## Building from source

See [DEVELOPERS.md](DEVELOPERS.md).

## License

Mango's source is published under the [PolyForm Noncommercial License 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0): free to use,
change and share for noncommercial purposes. The license grants no rights to the Mango name or logo.

Mango bundles `ffmpeg`/`ffprobe` binaries built under the LGPL license (no GPL-only components
enabled). See the [FFmpeg project](https://ffmpeg.org/legal.html) for details.
