# ============================================================
# Stage 1: build
# ============================================================
FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

# ---- build toolchain + Qt6 + GStreamer dev ----
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config \
    qt6-base-dev qt6-declarative-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-plugins-base libglib2.0-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# ---- source (cached unless CMakeLists / sources change) ----
COPY CMakeLists.txt ./
COPY main.cpp GstVideoReceiver.cpp GstVideoReceiver.h VideoItem.cpp VideoItem.h Main.qml ./

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
    qml6-module-qtquick qml6-module-qtquick-window \
    qml6-module-qtquick-controls qml6-module-qtquick-layouts \
    qml6-module-qtquick-templates qml6-module-qtqml-workerscript \
    libqt6core6 libqt6gui6 libqt6dbus6 libqt6network6 libqt6opengl6 \
    libqt6qml6 libqt6qmlcore6 libqt6qmllocalstorage6 libqt6qmlmodels6 libqt6qmlworkerscript6 libqt6qmlxmllistmodel6 \
    libqt6quick6 libqt6quickcontrols2-6 libqt6quickcontrols2impl6 libqt6quickdialogs2-6 libqt6quickdialogs2quickimpl6 libqt6quickdialogs2utils6 \
    libqt6quicklayouts6 libqt6quickparticles6 libqt6quickshapes6 libqt6quicktemplates2-6 libqt6quickwidgets6 \
    libgstreamer1.0-0 libgstreamer-plugins-base1.0-0 \
    gstreamer1.0-plugins-base \
    gstreamer1.0-plugins-good \
    gstreamer1.0-plugins-bad \
    gstreamer1.0-plugins-ugly \
    gstreamer1.0-libav \
    gstreamer1.0-x gstreamer1.0-gl gstreamer1.0-gtk3 \
    libgl1 libglib2.0-0 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# ---- copy only what's needed from the build stage ----
COPY --from=build /install/bin/centerDisplay ./centerDisplay
COPY --from=build /install/lib/qt6/qml/centerDisplay ./qml/centerDisplay

# non-root: map to the host user at runtime via --user
RUN useradd -m -s /bin/bash appuser

USER appuser
ENTRYPOINT ["./centerDisplay"]
