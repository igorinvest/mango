# Mango compared with VLC, Plex and Jellyfin

**Your movies on your TV in one click. No server, no account, no setup.**

Until now you had two choices. A video player like VLC is easy, but casting is an afterthought.
A media server like Plex or Jellyfin casts well, but you have to set it up, sign in, and keep it
running. Mango gives you the best of both, and nothing to look after.

- **As easy as a video player.** Install the app, pick a file, and it's on your TV. No Docker,
  no config files, no sign-up.
- **As capable as a media server.** A library of your folders, conversion only when your TV
  needs it, audio and subtitles in your language, whole seasons that play on their own, and
  your library on a TV in another home.
- **Nothing to maintain.** A native app for macOS, Windows and Linux that just runs. No containers,
  no ports to open, no users to manage, and it updates itself.

## Plays the way Chromecast was built to play

Most apps make Chromecast play their way. Mango plays Chromecast's way.

Chromecast has a built-in player made for **DASH**, a streaming format that sends video in short
pieces, with audio and subtitles as separate streams. Mango streams in DASH, straight into that
built-in player. The others don't: VLC sends one converted file, and Plex and Jellyfin load their
own receiver apps on your TV.

What you get from this:

- **The TV's own player.** Your TV shows the standard Cast player, not a third-party app.
- **Fast seeking.** The video is split into short pieces, so jumping to any point starts
  quickly.
- **Real track switching.** Video, audio and subtitles are separate streams, so changing the
  audio language or subtitles mid-movie works like it should.
- **Less conversion.** Mango copies what the TV can play and only converts what it can't, so
  picture quality stays high and the computer stays cool.

(On macOS, Mango streams to AirPlay in HLS, the format Apple devices speak natively.)

## Feature table

| | Mango | VLC | Plex | Jellyfin |
|---|---|---|---|---|
| Setup | Install, pick a file | Install, open a file | Install server, sign in, add libraries | Install server, create users, add libraries, set up remote access |
| Runs as | Native app (macOS, Windows, Linux); optional Linux service | Native app | Server + apps | Server, often in Docker |
| Account needed | No | No | Yes (Plex account) | Local users on your server |
| Docker needed | No | No | Optional | Common |
| Cast to Chromecast | Yes, DASH to the built-in player | Yes, one converted file | Yes, through the Plex receiver app | Yes, through the Jellyfin receiver app |
| Cast to AirPlay | Yes (macOS) | No | Yes (from Apple devices) | Limited |
| Media library | Your folders | No | Yes, with posters and info | Yes, with posters and info |
| Converts only what the TV can't play | Yes | Yes | Yes | Yes |
| Subtitle and audio files next to the video | Found and picked automatically | Opened by hand | Yes | Yes |
| Switch tracks during playback | Yes | Yes | Yes | Yes |
| Season plays episode after episode | Yes | With a playlist | Yes | Yes |
| Watch on a TV in another place | Pair two Mangos, no port forwarding | No | Yes (paid plan) | Port forwarding, reverse proxy or VPN |
| Telemetry | None | None | Yes | None |
| Cost | Free for personal use | Free | Free, some features paid | Free |

Plex and Jellyfin change often. Check their own sites for the latest plans and features.

## Mango vs VLC

VLC is great for playing files on your computer, and it can cast too. But casting in VLC is an
extra: it converts the whole file into one stream, and you open files one by one.

Mango is made for casting. It keeps a library of your folders, finds the subtitle and audio
files next to each video, picks your languages, plays a season through on its own, remembers
where you stopped, and can play your library on a TV in another place. It's still as simple as
VLC: install it and press Play.

## Mango vs Plex

Plex casts well, but it's a whole platform: a server, a Plex account you sign in to even at
home, and a paid plan to watch away from home.

Mango gives you the casting part without the rest. At home, nothing leaves your network: no
account, no sign-in, no usage data sent anywhere. Watching on a TV in another place is free:
pair two Mangos with a QR code and you're done. Mango's signaling server only introduces the two
devices. The video goes directly between them, encrypted end to end, so the server can't see
or change it. And the TV plays Mango's streams in its own built-in player, not a separate app.

## Mango vs Jellyfin

Jellyfin is free and open source, but you have to run it yourself: install the server, often in
Docker, create users, and set up port forwarding, a reverse proxy or a VPN to watch away from
home. Then keep it all updated.

Mango is a native app with zero setup. Remote streaming works without touching your router,
and the Windows app updates itself through the Microsoft Store.

## What Mango doesn't do

Mango does one job: getting your video files onto your TV. It doesn't try to be a full media
center, so it has:

- no posters, descriptions or trailers;
- no user profiles;
- no playback in a web browser, and no phone apps yet;
- no Docker image.

If you need those, Plex or Jellyfin are a better fit, and you can run them next to Mango on the
same folders. Mango only reads your files; it never moves or changes them.

## Learn more

- [README](README.md): what Mango does and how to use it
- [LINUX.md](LINUX.md): Mango on Linux, including running it as a service
