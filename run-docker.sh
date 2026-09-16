#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
: "${DISPLAY:?Run this script from your Ubuntu desktop terminal}"
command -v xauth >/dev/null || { echo 'Install xauth: sudo apt install xauth' >&2; exit 1; }
export HOST_UID="$(id -u)" HOST_GID="$(id -g)"
export CENTERDISPLAY_XAUTHORITY="$(mktemp /tmp/centerdisplay-xauth.XXXXXX)"
source_cookie="$(mktemp /tmp/centerdisplay-source-xauth.XXXXXX)"
cleanup() { rm -f -- "$CENTERDISPLAY_XAUTHORITY" "$source_cookie"; }
trap cleanup EXIT
# FamilyWild lets the same cookie work with the container hostname.
cp -- "${XAUTHORITY:-$HOME/.Xauthority}" "$source_cookie"
xauth -f "$source_cookie" nlist "$DISPLAY" | sed 's/^..../ffff/' | xauth -f "$CENTERDISPLAY_XAUTHORITY" nmerge -
if [ ! -s "$CENTERDISPLAY_XAUTHORITY" ]; then
    echo 'No X11 authentication cookie found. Check DISPLAY and XAUTHORITY.' >&2
    exit 1
fi
docker compose up --build --abort-on-container-exit
