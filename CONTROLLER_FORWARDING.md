# Z2-Flight to CenterDisplay over Tailscale

## Controller setup

1. Connect the controller and center PC to your Tailscale network. Read the **Tailscale IPv4** of each device from Tailscale.
2. Keep the camera's original RTSP URL in Z2-Flight and confirm local video works.
3. Open **Application Settings → Video → CenterDisplay over Tailscale**. In Vietnamese this section is **Truyền về CenterDisplay qua Tailscale**.
4. Enable **Forward main video**, enter the **controller's Tailscale IPv4** as the listen address, keep RTSP port **8554**, and press **Apply video forwarding**. Applying briefly restarts video reception and is disabled during recording. It does not change flight parameters or the vehicle connection.
5. Wait for the RTSP listening status. The server starts after it receives encoded video. In CenterDisplay's camera slot, enter:

   `rtsp://CONTROLLER_TAILSCALE_IPV4:8554/live`

   Replace the placeholder with the actual controller address. The camera does not need Tailscale. Video uses RTSP with TCP interleaving.
6. Enter the **center PC's Tailscale IPv4** as the telemetry destination, keep UDP port **14550**, and enable **Forward telemetry (send only)**. Telemetry settings apply to newly received aircraft messages immediately.
7. In CenterDisplay, open the UAV slot's **Map → Link** settings. Set UDP port **14550**, source IP to the **controller's Tailscale IPv4**, and system ID to the aircraft's MAVLink system ID (often 1). Connect the slot.

The two directions are different: CenterDisplay connects to the controller for video; the controller sends telemetry to the center PC.

A listen address of **0.0.0.0** accepts RTSP connections on all controller IPv4 interfaces. Binding to its Tailscale address limits listening to that interface; Tailscale must already be connected when you apply the setting. Keep Z2-Flight included in the Android VPN. Your Tailscale access rules and host firewall must allow TCP 8554 to the controller and UDP 14550 to the PC. No public internet port forwarding is needed.

For multiple controller/UAV pairs, use PC UDP ports 14550–14555 for CenterDisplay slots 1–6. Each controller can use RTSP port 8554 because it has a separate IP. A controller forwarding multiple aircraft to one port is filtered to the configured system ID by the receiving slot.

## Data paths and limits

```text
Camera RTSP → existing source parser → encoded tee
                                  ├─ existing decoder → controller display
                                  ├─ existing recording branch
                                  └─ bounded queue → appsink
                                                     ↓ encoded buffers
                         controller RTSP: appsrc → parser → RTP
                                                     ↓ Tailscale / TCP
                                           CenterDisplay video slot

Aircraft MAVLink → existing Z2-Flight vehicle processing
                 └─ observation-message allowlist → send-only UDP
                                                     ↓ Tailscale
                                           CenterDisplay telemetry slot
```

The video branch forwards compressed main-camera H.264/H.265 without re-encoding or a second camera connection. Thermal video and audio are not included. QGC's existing parser supplies AVC/HVC1 codec headers, allowing a viewer to join after the camera starts. Unsupported formats or a bind failure report an error while leaving the local display running.

Both the new tee queue and server input queue are bounded and nonblocking. A slow remote viewer cannot backpressure the local decoder through this branch. Dropped compressed frames can require the next camera keyframe to restore the remote picture. This feature does not change camera bitrate or keyframe settings; upload bandwidth should accommodate the camera bitrate for each viewer.

The controller forwards these received MAVLink messages: HEARTBEAT, SYS_STATUS, GPS_RAW_INT, ATTITUDE, GLOBAL_POSITION_INT, VFR_HUD and BATTERY_STATUS. System/component IDs, payloads, checksums and signatures are preserved. Commands, parameter writes and signing keys are excluded. Unsolicited UDP replies are discarded; this path cannot send commands to the UAV.

CenterDisplay decodes:

| Value | Model role | Unit |
|---|---|---|
| Position | latitude / longitude | degrees |
| Relative altitude | altitude | metres |
| AMSL altitude | altitudeAMSL | metres |
| Aircraft heading | heading | degrees; -1 means unknown |
| Ground speed | groundSpeed | m/s |
| Climb rate | climbRate | m/s, positive up |
| Roll / pitch | roll / pitch | degrees |
| Yaw | yaw | degrees, normalized to 0–360 |

The map slot rows display position, relative altitude, ground speed and roll/pitch/yaw. AMSL and climb rate are also available as model roles. Other forwarded messages are present on the UDP stream but are not yet exposed as CenterDisplay values.

Position and attitude become stale independently after five seconds. New attitude never makes an old map position appear live. Forwarding follows the aircraft's existing telemetry rate; it does not issue commands to increase that rate.

CenterDisplay checks CRC, configured system ID, autopilot component ID 1 and optional source IP. It accepts MAVLink v1/v2, including truncated v2 payloads. Its existing policy rejects signed frames because it does not verify signing keys. Do not disable aircraft signing as a workaround; signed telemetry needs separate receiver verification support. “Sending” means the local UDP send succeeded, not that delivery to the PC has been confirmed.

## Troubleshooting

- **RTSP listen failed:** verify the controller VPN address exists, or try 0.0.0.0. Check whether another service uses 8554, then Apply again.
- **Waiting for video:** confirm the original camera stream displays locally and uses H.264/H.265.
- **Remote video times out:** use the controller IP in the URL, check Tailscale connectivity/access rules, and allow TCP 8554.
- **Telemetry waiting:** check the PC destination IP, UDP port, source filter and system ID. The aircraft must emit GLOBAL_POSITION_INT and ATTITUDE from component 1.
- **One telemetry group stale:** position and attitude have separate timestamps; check the missing message stream.
- **Remote video stalls:** check upload bandwidth and whether Tailscale uses a direct connection or relay. Passthrough video does not reduce the source bitrate.
- **Disable:** telemetry can be disabled immediately. Disable video and press Apply, or stop source video. Apply is unavailable during recording.

## Code and validation

QGC implementation:

- `src/VideoManager/VideoReceiver/GStreamer/GstRtspForwarder.*`: isolated RTSP server and encoded tee branch.
- `GstVideoReceiver.*`: forwarding lifecycle for the primary video receiver.
- `src/Comms/CenterDisplayTelemetrySender.*`: observation-message allowlist and send-only UDP.
- `MAVLinkProtocol.*`: forwards successfully decoded messages from non-forwarding links.
- `src/Settings/VideoSettings.*`, `Video.SettingsGroup.json`, `src/UI/AppSettings/VideoSettings.qml`: persistent settings, explicit video Apply and status.
- GStreamer CMake adds app/RTSP-server libraries and the static app plugin for Android.

CenterDisplay changes are in `TelemetryModel.*`, `MapPanel.qml`, `tests/map_tests.cpp` and `tools/simulate_uavs.py`. The simulator now emits attitude and position.

Local verification passed:

- H.264 and H.265 decoded RTSP frames, reconnection, local branch progress without a viewer / with a slow consumer, shutdown with an active viewer and port reuse.
- Actual generated MAVLink frames sent through the QGC sender into CenterDisplay: position, attitude, units, unchanged wire bytes, invalid destinations and exclusion of commands/parameters/signing keys.
- CenterDisplay map/telemetry and settings UI tests, including malformed messages and independent freshness.
- Android release APK packaging and CenterDisplay Linux build.

Reproducible integration tests and logs are in QGC `local-artifacts/forwarding-tests`. The CMake test project uses Qt 6.8.3, GStreamer 1.20+ and a configurable sibling `CENTERDISPLAY_ROOT`. Host RTSP development headers are in its `host-deps` folder; the system installation was not changed. Android uses the project's existing cached GStreamer SDK.

Deploy/sign Android using the existing Qt Creator configuration. The built APK is `build/Android_Qt_6_8_3_Clang_arm64_v8a-Release/android-build/build/outputs/apk/release/android-build-release-unsigned.apk`. CenterDisplay is `build/map-check/bin/centerDisplay`.

Physical camera behavior, Android Tailscale routing and controller-to-PC internet performance remain to be verified on your devices. Local loopback tests and build success do not replace that check.

## Center-to-controller flight-plan drafts

Flight plans now use a separate authenticated inbox on TCP **8767**, not the
send-only telemetry channel above. The center can send a route for review; the
operator loads it in Z2-Flight Plan view, reviews and uploads it using the
existing controls. Receipt does not start flight. See
[RESCUE_DEMO.md](docs/RESCUE_DEMO.md) for configuration and validation boundaries.
