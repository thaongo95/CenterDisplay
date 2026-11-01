# CenterDisplay with Docker

## Ubuntu

Requirements: Docker Engine with Compose, a graphical X11 or Xwayland session,
and `xauth` (`sudo apt install xauth` if missing). Your user must be able to run
`docker info` successfully.

From your desktop terminal:

```bash
cd pathtofolder/CenterDisplay
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
Camera URLs and the 1/4/6 layout are saved when you click Apply and restored on restart.

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

## Receiver reliability update (Phase 1)

Each receiver now owns a worker thread for pipeline setup/teardown, bus polling,
and reconnect timers. Qt polls the GStreamer bus every 50 ms on Linux and Windows;
no GLib main loop is required in the application. Initial/no-frame timeout is ten
seconds. Retry delays increase from 1 to 2 to 4 seconds, capped at 5 seconds;
ten seconds of valid video resets the delay. Stop cancels retry/watchdog activity,
and request/attempt IDs reject callbacks from replaced connections. The connected
property becomes true only after a valid decoded video frame.

RTSP pad selection accepts only `media=video`; decoded frames are mapped with their
actual row stride. The receiver supports the application's camera layouts.
Pipeline setup failures emit `errorOccurred`; invalid URLs are rejected before start.

See `tests/README.md` for automated local RTSP integration tests. Windows builds
link explicit GStreamer import libraries so the SDK's static compiler runtime is
not accidentally linked. Portable packaging must use the matching Qt/MinGW compiler
runtime DLLs. Application shutdown joins the worker threads to release pipelines
safely; final process exit may wait for a camera connection to finish tearing down.


## Six cameras and saved URLs

The application supports 1, 4 and 6 screens (1x1, 2x2 and 3x2). Every screen can
select any of the six cameras. Open the gear button to edit all six URLs in a
scrollable editor. Click **Áp dụng (Apply)** to save URLs and the selected mode;
**Hủy (Cancel)** discards unsaved edits.

Native builds save an INI file through QSettings in the current user's application
configuration location, so moving/updating the run folder does not erase values.
Docker launchers create `config/` beside the project and mount it at `/config`,
with `XDG_CONFIG_HOME=/config`; settings survive container replacement. This folder
is excluded from Git and Docker build uploads. Keep it when updating the project.

Saving a URL does not automatically restart an already-running stream. Select the
camera again in its screen selector to connect using the changed URL.

## Corrupt-stream recovery and controls

Open Settings to find a Stop/Start button and colored connection rectangle beside
each camera URL: gray Stopped, amber Connecting, green Live, red Reconnecting.
Controls follow camera assignments, including a camera shown on multiple screens;
Stop clears all assigned screens and cancels their retries. Start uses the URL
currently typed in that row; Apply persists it. Cancel discards unsaved URL/layout
edits but does not undo Start/Stop actions. If no screen is assigned to a camera,
Start assigns its numbered screen (possibly replacing that screen's camera).
Changing to layout 1 or 4 does not stop streams in hidden screens. Use Settings
Stop to release their resources. A row reports Reconnecting before Connecting,
then Live, then Stopped when its assigned screens have mixed states.

Only the newest decoded frame is retained for presentation (up to 30 fps), so a
slow GUI does not accumulate video in its event queue. Corrupted/decode-only,
truncated, invalid-stride and oversized decoded frames are discarded. Frames are
limited to 8192 pixels per dimension and 128 MiB. Isolated bad frames do not tear
down the stream; ten seconds without valid frames triggers reconnection. A frozen
last picture remains visible during reconnect, with the Reconnecting status visible in Settings.

Tests inject damage into real H.264 RTP packets, stall video, restore service,
and exercise a healthy camera alongside a failing one. This handles recoverable
GStreamer errors, not native decoder segmentation faults: decoders still share
the application process. Process isolation would be required to contain those.

See `MACHINE_REQUIREMENTS.md` for the original nine-stream capacity estimates; the current fleet uses six streams.

## Six-UAV map and telemetry

Choose **Videos** or **Map** in the top toolbar. The map-type selector
provides **Street (OpenStreetMap)**, **Satellite (Esri)**, **Topographic (Esri)**,
and **Custom tile server**. This uses Qt Location/Positioning, the same map UI
framework used by QGroundControl, but does not bundle QGC's complete provider
plugin or its offline map manager. Google/Bing account-specific integrations are
not included. Custom providers accept HTTPS XYZ templates such as
`https://tiles.example.com/{z}/{x}/{y}.png` (including a provider token if needed).
Use a provider you are authorized to access and retain its required attribution.
Online tiles need internet access; downloaded tiles are cached per provider.

### Connect each ground station

Telemetry is separate from RTSP: video alone contains no UAV position.

1. On each GCS, enable QGC **Settings → Telemetry → MAVLink forwarding** (older
   QGC versions call this section **MAVLink**).
2. Forward to the CenterDisplay computer's reachable VPN/Tailscale IPv4 address:

   | UAV / camera | Destination UDP port |
   |---|---|
   | 1 | 14550 |
   | 2 | 14551 |
   | 3 | 14552 |
   | 4 | 14553 |
   | 5 | 14554 |
   | 6 | 14555 |

3. In the map's **Link** dialog for each UAV, set its MAVLink system ID
   (default `1`) and optionally restrict the source to its GCS Tailscale IPv4.
   Every UAV has a dedicated port, so separate UAVs may all use system ID 1.
   Saved settings are restored on restart. Ports must be unique and available.
4. Allow these UDP ports through the receiving PC firewall and VPN ACLs.
   The GCS sends to this PC; the RTSP TCP relay does not relay this telemetry.
   With ordinary internet/NAT, use a routed VPN or configure UDP forwarding to
   the receiving PC. Windows Docker publishes the default six ports; if you
   change ports in the UI, update Compose mappings as well. Source-IP filters
   must match the address actually seen inside the container after NAT.
5. Ensure the autopilot sends `GLOBAL_POSITION_INT` (message 33) from component
   ID 1, typically at 1–5 Hz. CenterDisplay is receive-only and does not request
   message rates or send flight commands. Set rates at the autopilot/GCS.

Click a UAV to center on its last position; **Follow** tracks the selected UAV.
Markers and rows show live/stale state, coordinates, and altitude relative to
home. Heading is shown when available. Positions older than five seconds turn
gray and retain their last location; no position means no marker. A (0,0)
position is treated as uninitialized. Do not use the display as flight control.

The receiver checks MAVLink v1/v2 checksums, system/component IDs, coordinate
ranges and source filters. It supports complete frames (including multiple
frames) per UDP datagram and MAVLink 2 truncated payloads. It rejects signed
frames because signing-key verification is not implemented. Use a trusted VPN
for unsigned forwarded telemetry; signed-only streams need a supported bridge.
Actual end-to-end GCS forwarding and camera hardware require field testing.

### Build and test the map

Install Qt **Location**, **Positioning**, **Network**, **Quick**, and **QML**
modules in addition to the existing GStreamer dependencies. Docker downloads Qt 6.8.3 with Location/Positioning because Ubuntu 24.04's
Qt 6.4 packages do not provide Qt Location 6. The updated Docker image has
not been rebuilt as part of the native map validation. For a Windows native deployment, rerun
`windeployqt --qmldir .` and include Qt Location/Positioning QML modules and the
OSM geoservices plugin; older portable ZIPs do not contain this feature.

```bash
cmake -S . -B build/map-check -DCMAKE_PREFIX_PATH=/path/to/Qt/6.8.3/gcc_64 \
  -DCENTERDISPLAY_BUILD_MAP_TESTS=ON
cmake --build build/map-check -j4
ctest --test-dir build/map-check --output-on-failure
./build/map-check/bin/centerDisplay
```

Map tests exercise six UDP slots, v1/v2 positions, truncated payloads, corrupt
packets, invalid coordinates, ID filtering, stale positions, and QML map-type
switching. They require loopback UDP socket permissions. Existing RTSP server
integration tests remain available through `CENTERDISPLAY_BUILD_TESTS`.

References: [QGC telemetry forwarding](https://docs.qgroundcontrol.com/master/en/qgc-user-guide/settings_view/telemetry.html),
[QGC map settings](https://docs.qgroundcontrol.com/master/en/qgc-user-guide/settings_view/maps.html),
[MAVLink GLOBAL_POSITION_INT](https://mavlink.io/en/messages/common.html#GLOBAL_POSITION_INT).

### Watch six simulated UAVs

From the project directory, after building the app:

```bash
python3 tools/simulate_uavs.py --launch
```

The app opens directly in Map view with **SIMULATION** in its title. Click
**Fit** to see six UAVs flying circular routes in a 2×3 grid near Ho Chi Minh
City. Click a UAV and enable **Follow** to track it. Choose Satellite or another
map type using the combo box. Close the window or press Ctrl+C to finish.
Python 3 is sufficient; no pip packages or UAV hardware are required.
The `--launch` convenience option targets the native Linux app and uses temporary
settings plus ports 24550–24555 with a localhost source filter. Your normal map,
camera, and GCS settings are preserved. Keep those demo ports free.

To watch telemetry loss, run:

```bash
python3 tools/simulate_uavs.py --launch --duration 20
```

After 20 seconds the sender stops but the map window remains open. Five seconds
later, markers become gray and their rows show Stale. Close the window to exit.

For an already running app, use `python3 tools/simulate_uavs.py` (default ports
14550–14555). Its six Link settings must use system ID 1 and accept source
127.0.0.1. Use the isolated launch above when your normal app is receiving real
UAV telemetry. `--base-port`, `--host`, and `--app` customize the destination or
executable; `--help` describes each option. The sender can also run on Windows
with Python, but automatic temporary configuration/launch is Linux-specific.

#### How the simulation and tests work

`tools/simulate_uavs.py` calculates latitude, longitude, relative altitude and
heading for six circles. It packs these into MAVLink `GLOBAL_POSITION_INT`
(message 33), computes the checksum, and sends one UDP packet per UAV every
0.2 seconds. The app receives and decodes these through `TelemetryModel`, exactly
as it receives forwarded GCS position packets, then QML updates the markers.
This is a telemetry demonstration, not an autopilot/flight-physics simulator,
and does not generate RTSP video.

The earlier `tests/map_tests.cpp` tests sent fixed known positions and checked
the decoded coordinates, six-slot routing, checksum/ID rejection and stale
state. The new `pythonSimulator` test launches this Python sender and verifies
that all six slots become live and positions actually change. Run it alone:

```bash
QT_QPA_PLATFORM=offscreen ./build/map-check/bin/map_tests pythonSimulator
```

Or run both map and settings suites with:

```bash
ctest --test-dir build/map-check --output-on-failure
```

### Location connection controls and automatic zoom

Each UAV row has a **Connect / Disconnect** button beside **Link**. Disconnect
closes only that UAV's UDP location listener and removes its position marker;
its RTSP video is unaffected. Connect binds its saved port again and waits for
fresh MAVLink positions. Bind failures appear in the UAV's status row. Editing
Link settings while disconnected does not reconnect it. As before, location
listeners start automatically when the app is reopened; manual disconnections
apply to the current session.

When the map first receives live positions, it automatically centers and zooms:
one UAV uses zoom level 16; multiple UAVs are fitted together (capped at 16).
Normal updates preserve your chosen pan/zoom. Automatic framing is enabled again
once all positions are stale or disconnected, and runs when live positions
return. **Fit** remains available whenever you want to frame the fleet manually.

### Video and map toolbars

Only **Videos** and **Map** modes are available. Videos shows camera selectors
and the RTSP settings button. Map shows the map type, Fit, zoom, Follow, and
**Hide locations / Show locations** controls in the top toolbar.
The location button hides or expands the entire bottom UAV information panel;
it does not stop telemetry or remove map markers. The expanded panel fits all
six rows when space permits and scrolls on smaller windows. Switching modes
preserves the map and the panel's shown/hidden state during the session.

## PostgreSQL rescue Map–List–Record example

The fleet now contains **six UAVs: R1–R3 reconnaissance and T1–T3 transport**.
The map includes ten fictional priority locations, flood polygons, PostgreSQL
activity records, reconnaissance routes and priority-based transport drafts.

Run `bash tools/run-rescue-demo.sh` after building. See
[the full walkthrough](docs/RESCUE_DEMO.md) for database setup, the simulator,
Map–List–Record behavior, and the new Z2-Flight flight-plan inbox.
