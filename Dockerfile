# ============================================================
# Stage 1: build
# ============================================================
FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

# ---- build toolchain + Qt6 + GStreamer dev ----
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config \
    python3-venv ca-certificates libgl1-mesa-dev libxkbcommon-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-plugins-base libglib2.0-dev \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu 24.04's Qt 6.4 packages do not include Qt Location 6.
# Use the same complete Qt 6.8.3 SDK as the native build.
RUN python3 -m venv /opt/aqt \
    && /opt/aqt/bin/pip install --no-cache-dir aqtinstall==3.3.0 \
    && /opt/aqt/bin/aqt install-qt linux desktop 6.8.3 linux_gcc_64 \
       -O /opt/Qt -m qtlocation qtpositioning qtshadertools
ENV CMAKE_PREFIX_PATH=/opt/Qt/6.8.3/gcc_64

WORKDIR /src

# ---- source (cached unless CMakeLists / sources change) ----
COPY CMakeLists.txt ./
COPY main.cpp AppSettings.cpp AppSettings.h GstVideoReceiver.cpp GstVideoReceiver.h VideoItem.cpp VideoItem.h VideoFrame.h TelemetryModel.cpp TelemetryModel.h RescueStore.cpp RescueStore.h Main.qml MapPanel.qml RescuePanel.qml ./

# ---- configure + build ----
RUN cmake -S . -B build \
      -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/install \
   && cmake --build build \
   && cmake --install build

# ============================================================
# Stage 2: runtime
# ============================================================
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV QT_QPA_PLATFORM=xcb

# ---- runtime libs: Qt6 minimal + GStreamer + plugins ----
# plugins-good/bad/ugly/libav cover rtspsrc, decodebin fallbacks,
# videoconvert, and software decoders for whatever the cameras send.
RUN apt-get update && apt-get install -y --no-install-recommends \
    libpq5 ca-certificates libfontconfig1 libfreetype6 libdbus-1-3 libxkbcommon0 \
    libxkbcommon-x11-0 libxcb-cursor0 libxcb-icccm4 libxcb-image0 \
    libxcb-keysyms1 libxcb-render-util0 libxcb-xinerama0 libxcb-xkb1 \
    libxcb-shape0 libxcb-randr0 libegl1 libopengl0 \
    libgstreamer1.0-0 libgstreamer-plugins-base1.0-0 \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav \
    gstreamer1.0-x gstreamer1.0-gl gstreamer1.0-gtk3 \
    libgl1 libglib2.0-0 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /opt/Qt/6.8.3/gcc_64/lib /opt/Qt/6.8.3/gcc_64/lib
COPY --from=build /opt/Qt/6.8.3/gcc_64/plugins /opt/Qt/6.8.3/gcc_64/plugins
COPY --from=build /opt/Qt/6.8.3/gcc_64/qml /opt/Qt/6.8.3/gcc_64/qml
ENV LD_LIBRARY_PATH=/opt/Qt/6.8.3/gcc_64/lib
ENV QT_PLUGIN_PATH=/opt/Qt/6.8.3/gcc_64/plugins
ENV QML_IMPORT_PATH=/opt/Qt/6.8.3/gcc_64/qml

WORKDIR /app

# ---- copy only what's needed from the build stage ----
COPY --from=build /install/bin/centerDisplay ./centerDisplay
COPY --from=build /install/lib/qt6/qml/centerDisplay ./qml/centerDisplay

# non-root: map to the host user at runtime via --user
RUN useradd -m -s /bin/bash appuser

USER appuser
ENTRYPOINT ["./centerDisplay"]
