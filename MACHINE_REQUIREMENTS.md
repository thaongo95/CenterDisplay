# CenterDisplay machine requirements

## Scope and confidence

Sizing estimate for **nine simultaneous receiver pipelines**, not a certified
minimum or a measured nine-camera benchmark. The camera resolution, frame rate,
codec, bitrate, GOP, and scene complexity were not supplied. Baseline: nine
1920x1080 streams, H.264, 30 FPS, 4 Mbps each, one 1920x1080 application window,
no recording or analytics. More than one screen selecting the same camera still
creates separate decoder pipelines. Smaller layouts do not stop hidden streams.

The inspected host has an Intel Core i7-11700, 8 physical cores / 16 threads,
and about 31 GiB usable RAM (32 GB installed-class capacity). This is a reasonable
baseline test host for nine 1080p feeds, but its smooth playback is **unverified**.
No camera credentials or private stream URLs were used for these estimates.

## Calculated video workload

All cases below assume 9 cameras at 30 FPS. Bitrates are explicit scenario
assumptions, not codec guarantees. GB/s and Mbps are decimal; MiB is binary.

| Resolution per camera | Assumed bitrate each | Total compressed input | Network budget with 25% margin | Decode rate | BGRA pixel payload / second | Two owned BGRA frames per receiver |
|---|---:|---:|---:|---:|---:|---:|
| 1280x720 | 2 Mbps | 18 Mbps | 22.5 Mbps | 248.8 million pixels/s | 1.00 GB/s | 63.3 MiB |
| 1920x1080 | 4 Mbps | 36 Mbps | 45 Mbps | 559.9 million pixels/s | 2.24 GB/s | 142.4 MiB |
| 3840x2160 | 12 Mbps | 108 Mbps | 135 Mbps | 2,239.5 million pixels/s | 8.96 GB/s | 569.5 MiB |

Formulas:

- Pixels per second = camera count x width x height x FPS.
- BGRA payload per second = pixels per second x 4 bytes.
- Compressed network planning budget = sum of camera bitrates x 1.25.
- Two-image storage = camera count x width x height x 4 x 2 / 1024^2 MiB.

For 1080p, one owned image is 7.91 MiB. The latest-frame mailbox and displayed
image can retain two different frames per receiver. **142.4 MiB is not total
application RAM**: decoder reference frames, GStreamer pools, transient copies,
render surfaces/textures, Qt, libraries, and the OS add memory. The current frame
copy alone reads and writes about 4.48 GB/s at nine 1080p/30 streams; conversion
and rendering add further traffic. At nine 4K/30 streams that copy traffic is
about 17.92 GB/s. This is internal memory traffic, not Ethernet traffic.

15 FPS halves the pixel/copy workload, but does not necessarily halve compressed
bitrate unless camera encoding settings change. 60 FPS doubles decode/copy work;
the application's ~30 FPS presentation timer does not cap incoming decode work.
The 8192-pixel-per-dimension / 128 MiB frame safety limits are not performance
ratings. There is no single finite worst-case requirement without an input FPS,
codec and resolution limit.

## Practical baseline to test: nine 1080p/30 H.264 feeds

These are engineering planning targets, not hardware benchmark results:

- CPU: start with a recent desktop-class **8-core / 16-thread CPU**. The current
  i7-11700 is a useful starting point; do not buy a replacement before testing it.
  Core count alone cannot guarantee performance, especially with H.265 or complex
  streams. Reserve roughly 25-30% CPU headroom during the full workload.
- RAM: **16 GB system RAM** as a planning target; **32 GB** gives more room for
  other applications and decoder memory. Plan an initial **2-4 GB application
  allowance**, then replace that allowance with measured peak working set. This
  is a budget, not predicted consumption. Existing 32 GB host needs no assumed
  RAM upgrade for this baseline.
- Network: wired **1 Gbps Ethernet** and a switch with sufficient aggregate
  bandwidth. Budget using actual configured peak bitrates, not average bitrate.
  The sample 1080p scenario needs about 45 Mbps including planning margin; nine
  4K streams at the example bitrates already exceed a 100 Mbps link's capacity.
- GPU: no discrete GPU is specified for this software-rendered baseline. The
  Windows package includes software decoder plugins, not a configured hardware
  decoder path; Start-CenterDisplay.cmd forces Qt software rendering. Buying a
  stronger GPU alone will not move these decoders to the GPU.
- Disk: keep **1 GB free for the portable package/update allowance**, separate
  from OS headroom. The app does not record video, so no continuous recording
  disk bandwidth is required. A ZIP's compressed size is not its extracted size.

For nine 4K/30 feeds, do not extrapolate an assured CPU model from core count.
Pixel throughput is four times the 1080p case. Prefer camera substreams for the
nine-tile view, or implement and benchmark hardware decoding before selecting
hardware for that workload. On a 1080p monitor, each 3x3 tile is only about
640x360 pixels before borders/toolbars, although the current app still decodes
and copies the camera's full original resolution.

## Docker implications

Linux Docker uses the host CPU and memory. It does not provide video acceleration
by itself. The current launcher uses software rendering, and the compose setup
does not establish a GPU decode path. Avoid restrictive CPU/memory limits until
measuring the nine-stream workload. A native Windows deployment does not need
Docker. A Windows container is not part of this deployment.

## Validation needed before calling it smooth

1. Connect all nine real cameras at the intended settings and use the intended
   monitor resolution. Run for at least 30 minutes, including busy/moving scenes.
2. Measure total CPU, per-core saturation, application working set, dropped frames,
   end-to-end video delay and network peaks. Check that memory stops growing.
3. Aim for sustained total CPU below roughly 70-75%, no continuously saturated
   decoding thread that causes visible drops, no swapping, and stable latency.
4. Disconnect/reconnect several cameras while the other feeds continue; include
   simultaneous recovery and keyframe bursts in the peak measurements.
5. If overloaded, lower camera substream resolution/FPS first, then profile
   decoding versus conversion/rendering. More RAM will not fix CPU saturation.

The existing automated corruption/reconnect tests validate behavior with small
synthetic streams. They do **not** validate full-load performance or prove these
hardware targets sufficient.

## Technical references

GStreamer chooses compatible decodebin factories by rank; hardware use depends
on installed/configured decoders, not simply the presence of a GPU:
[decodebin documentation](https://gstreamer.freedesktop.org/documentation/playback/decodebin.html).
Appsink queue limits/drop settings bound its queue but do not remove upstream
video decoding work:
[appsink documentation](https://gstreamer.freedesktop.org/documentation/app/appsink.html).
Numeric workload estimates above are calculated from this app's BGRA frame path.
