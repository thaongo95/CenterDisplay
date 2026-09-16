#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
if [[ ! -S /mnt/wslg/.X11-unix/X0 ]]; then
    echo 'WSLg display is missing. Run wsl --update in PowerShell, then wsl --shutdown and retry.' >&2
    exit 1
fi
if ! command -v docker >/dev/null || ! docker info >/dev/null 2>&1; then
    echo 'Start Docker Desktop and enable WSL integration for this Ubuntu distribution.' >&2
    exit 1
fi
if [[ "$(docker info --format '{{.OSType}}')" != linux ]]; then
    echo 'Switch Docker Desktop to Linux containers.' >&2
    exit 1
fi
export HOST_UID="$(id -u)" HOST_GID="$(id -g)"
export DISPLAY=:0
exec docker compose -p centerdisplay-windows -f docker-compose.windows.yml up --build --abort-on-container-exit
