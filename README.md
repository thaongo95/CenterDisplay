# CenterDisplay with Docker

## Ubuntu

Requirements: Docker Engine with Compose, a graphical X11 or Xwayland session,
and `xauth` (`sudo apt install xauth` if missing). Your user must be able to run
`docker info` successfully.

From your desktop terminal:

```bash
cd /home/b6/Works/CenterDisplay
bash run-docker.sh
```

The launcher builds the image and opens the application on your desktop. It
copies the current display cookie into a temporary file mounted read-only,
runs as your user, and removes the file when Compose exits. No `xhost` change
is needed. Close the window or press Ctrl+C to stop. Run the same command again
to rebuild after source changes.

The container uses host networking so RTSP cameras are reached through the
host's existing routes/VPN. Choose a camera in the screen selector to start
video; camera streams do not start automatically. The gear button edits URLs.
Camera URLs are currently held in memory and reset on restart.

Qt Quick uses software rendering for portability across Ubuntu graphics drivers;
GPU passthrough is not required. GStreamer includes software video decoders.

For logs while running:

```bash
docker logs --tail 100 centerdisplay
```

If no window appears, check `echo "$DISPLAY"`, `echo "$XAUTHORITY"`, and
`xauth info` in your desktop terminal. A missing cookie is reported by the
launcher. Camera timeouts require checking camera power, RTSP URLs and VPN/routes.


## Windows 11 with Docker Desktop

The same Ubuntu container image runs on Windows through Docker Desktop's Linux
container engine. WSLg displays the Qt window on the Windows desktop.

### One-time setup on the Windows PC

1. In administrator PowerShell, run `wsl --install -d Ubuntu`. Restart if
   requested, then open Ubuntu once and finish creating your Linux user.
   If Ubuntu is already installed, check `wsl -l -v` and use
   `wsl --set-version Ubuntu 2` if it is version 1.
2. Run `wsl --update` in PowerShell to update WSL/WSLg.
3. Install/start Docker Desktop. Enable **Use the WSL 2 based engine** under
   Settings > General. Under Settings > Resources > WSL Integration, enable
   **Ubuntu** and apply the changes. Use **Linux containers**.
4. Copy this entire project folder to a local Windows folder, for example
   `C:\Works\CenterDisplay`. Internet access is required for the first build.
5. Double-click `run-windows.cmd`. Leave its console open while using the app.
   Close the app or press Ctrl+C in the console to stop it.

The launcher expects the WSL distribution name `Ubuntu`. If yours has a different
name (see `wsl -l -q`), change `--distribution Ubuntu` in `run-windows.cmd`.
Alternatively, open your WSL distribution and run:

```bash
cd /mnt/c/Works/CenterDisplay
bash run-windows.sh
```

`docker-compose.windows.yml` is a standalone Compose file. It mounts WSLg's X11
socket and uses Docker's default bridge network. No X server installation,
Windows Qt/GStreamer installation, or GPU passthrough is needed.

RTSP connections use TCP. Cameras must be reachable from Docker Desktop; check
Windows VPN routes and firewall rules if the window opens but video does not.
Choose a camera from the screen selector to start playback. Software rendering
and decoding are used, so multiple high-resolution streams may use substantial CPU.

### Validation status

The Ubuntu image build and desktop launch were verified on Ubuntu. Windows
launcher syntax and Compose configuration were checked on Linux; actual WSLg
window display must still be tested on a Windows PC.

References:
- https://learn.microsoft.com/en-us/windows/wsl/tutorials/gui-apps
- https://docs.docker.com/desktop/features/wsl/use-wsl/
