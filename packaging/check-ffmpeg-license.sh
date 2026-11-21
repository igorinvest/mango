#!/bin/sh
# Fails when an ffmpeg Mango bundles was built with GPL or non-free parts
# (docs/scope/platform-support-scope.md, decision 1: the bundle stays LGPL).
#   packaging/check-ffmpeg-license.sh build-linux-x64-release/ffmpeg
set -eu

ffmpeg="${1:?usage: check-ffmpeg-license.sh <path to the bundled ffmpeg>}"
configuration=$("$ffmpeg" -hide_banner -buildconf)

if printf '%s\n' "$configuration" | grep -qE -- '--enable-(gpl|nonfree)'; then
    echo "$ffmpeg is built with --enable-gpl or --enable-nonfree; the bundled ffmpeg must stay LGPL." >&2
    exit 1
fi
echo "$ffmpeg: LGPL build (no --enable-gpl, no --enable-nonfree)"
