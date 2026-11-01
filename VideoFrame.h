#pragma once
#include <gst/video/video.h>
#include <QImage>
#include <cstring>

// Copy only validated, owned BGRA pixels across the GStreamer/Qt boundary.
inline QImage copyVideoFrame(GstSample *sample)
{
    if (!sample) return {};
    GstCaps *caps = gst_sample_get_caps(sample);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstVideoInfo info;
    gst_video_info_init(&info);
    if (!caps || !buffer || GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_CORRUPTED) ||
        GST_BUFFER_FLAG_IS_SET(buffer, GST_BUFFER_FLAG_DECODE_ONLY) ||
        !gst_video_info_from_caps(&info, caps) ||
        GST_VIDEO_INFO_FORMAT(&info) != GST_VIDEO_FORMAT_BGRA) return {};
    const int width = GST_VIDEO_INFO_WIDTH(&info);
    const int height = GST_VIDEO_INFO_HEIGHT(&info);
    // Bound allocation even if a corrupt stream advertises unreasonable dimensions.
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192 ||
        quint64(width) * height * 4 > 128 * 1024 * 1024) return {};
    GstVideoFrame mapped;
    if (!gst_video_frame_map(&mapped, &info, buffer, GST_MAP_READ)) return {};
    const auto *pixels = static_cast<const uchar *>(GST_VIDEO_FRAME_PLANE_DATA(&mapped, 0));
    const int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&mapped, 0);
    QImage image;
    const auto base = reinterpret_cast<quintptr>(mapped.map[0].data);
    const auto start = reinterpret_cast<quintptr>(pixels);
    const quint64 bytes = stride > 0 ? quint64(height - 1) * stride + quint64(width) * 4 : 0;
    if (pixels && stride >= width * 4 && start >= base &&
        start - base <= mapped.map[0].size && bytes <= mapped.map[0].size - (start - base)) {
        image = QImage(width, height, QImage::Format_ARGB32);
        if (!image.isNull())
            for (int row = 0; row < height; ++row)
                std::memcpy(image.scanLine(row), pixels + quint64(row) * stride, width * 4);
    }
    gst_video_frame_unmap(&mapped);
    return image;
}
