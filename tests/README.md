# Receiver integration tests

On Ubuntu, install the Qt 6 development packages, CMake, Ninja, a C++ compiler,
GStreamer development packages, `libgstrtspserver-1.0-dev`, and the GStreamer
base/good/bad/ugly/libav plugin packages. The synthetic server needs x264 and A-law.

```bash
cmake -S . -B build-tests -G Ninja -DCENTERDISPLAY_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

CTest runs with `QT_QPA_PLATFORM=offscreen` and `QT_NO_GLIB=1` so receiver message
handling cannot accidentally depend on Qt's optional GLib integration. The test
RTSP server uses a separate GLib context/thread and binds to loopback only.

Coverage:
- Missing GStreamer plugins produce an actionable error.
- Invalid URLs are rejected.
- Initial connection failure retries before any frame has arrived.
- Stop returns promptly and cancels scheduled retries and queued frames.
- A camera advertising audio first still delivers video.
- Server loss and return recover automatically.
- Switching during retry and rapid stop/start do not revive the old connection.

Timing uses the production five-second retry interval. Physical cameras, VPN
routing and Windows hardware must be checked separately.


The `settings_ui` test additionally checks a real QML window with nine fake
receivers: all 1/4/9 layouts, camera-9 routing to screen 1, all nine URL fields,
Apply/Cancel behavior, and INI persistence across a separate process. It writes
only to a temporary settings directory. Optional `CENTERDISPLAY_TEST_SCREENSHOTS`
selects a directory for grid/editor snapshots. QML runtime modules for QtQuick,
Controls, Layouts, Templates, Window and Qml WorkerScript must be installed.
