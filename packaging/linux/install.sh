#!/bin/sh
# Installs Mango on Debian or Ubuntu (x64) from the GitHub releases:
#
#   curl -fsSL https://raw.githubusercontent.com/igorinvest/mango/main/packaging/linux/install.sh | sh
#
# Picks the newest release (pre-releases included) that has a Linux package.
# Environment:
#   MANGO_VERSION=<tag>        install that release instead
#   MANGO_SYSTEM_FFMPEG=1      the package that uses the system ffmpeg
#                              (installs the ffmpeg package) instead of
#                              bundling its own
#   MANGO_NO_SERVICE=1         don't enable the background service
# Uninstall: sudo apt remove mango
set -eu

REPO="igorinvest/mango"
service_enabled=0

Fail() {
    echo "mango install: $*" >&2
    exit 1
}

CheckSystem() {
    command -v apt-get > /dev/null || Fail "needs Debian or Ubuntu (apt-get not found)."
    command -v curl > /dev/null || Fail "needs curl: sudo apt install curl"
    [ "$(dpkg --print-architecture)" = "amd64" ] || Fail "only x64 (amd64) is supported so far."
}

AssetName() {
    if [ "${MANGO_SYSTEM_FFMPEG:-}" = "1" ]; then
        echo "mango-system-ffmpeg_amd64.deb"
    else
        echo "mango_amd64.deb"
    fi
}

# The download URL of $1 in MANGO_VERSION's release, or in the newest
# release that has it.
AssetUrl() {
    if [ -n "${MANGO_VERSION:-}" ]; then
        echo "https://github.com/$REPO/releases/download/$MANGO_VERSION/$1"
        return
    fi
    curl -fsSL "https://api.github.com/repos/$REPO/releases" \
        | grep -o "https://github.com/$REPO/releases/download/[^\"]*/$1" \
        | head -n 1
}

# The systemd user service, started now and at boot (linger: without a
# login too). `mango` in a terminal attaches to it.
EnableService() {
    if [ "${MANGO_NO_SERVICE:-}" = "1" ]; then
        return
    fi
    if systemctl --user daemon-reload && systemctl --user enable --now mango \
        && sudo loginctl enable-linger "$(id -un)"; then
        service_enabled=1
    else
        echo "Couldn't enable the background service; see \"Run in the background\" in LINUX.md."
    fi
}

PrintNextSteps() {
    echo
    if [ "$service_enabled" = "1" ]; then
        echo "Mango is installed and running in the background."
    else
        echo "Mango is installed."
    fi
    echo "Add your videos and pick a TV in the console:"
    echo "  mango"
    echo "  mango> add-folder ~/Videos"
    echo "  mango> help"
    if [ "$service_enabled" = "1" ]; then
        echo "The console runs its commands in the service; quit leaves it running."
    fi
    if [ ! -e /dev/dri ] && [ "${MANGO_SYSTEM_FFMPEG:-}" != "1" ]; then
        echo
        echo "No GPU found (/dev/dri): converting video needs the system ffmpeg:"
        echo "  sudo apt install ffmpeg"
    fi
}

Main() {
    CheckSystem
    asset="$(AssetName)"
    url="$(AssetUrl "$asset")"
    [ -n "$url" ] || Fail "no release of $REPO has $asset yet."

    dir="$(mktemp -d)"
    trap 'rm -rf "$dir"' EXIT
    # apt reads the file as its own _apt user.
    chmod 755 "$dir"

    echo "Downloading $url"
    curl -fL --progress-bar -o "$dir/$asset" "$url"
    sudo apt-get install -y "$dir/$asset"
    EnableService
    PrintNextSteps
}

Main
