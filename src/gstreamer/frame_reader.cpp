#include "gstreamer/frame_reader.h"
#include "gstreamer/gst_ptr.h"

#include <gst/video/video.h>
#include <stdexcept>

namespace streambrew::gst {

void deliver_frame(GstSample *sample, PixelFormat format,
                   const std::function<void(const Frame &)> &on_frame) {
    GstVideoInfo video_info;
    if (!gst_video_info_from_caps(&video_info, gst_sample_get_caps(sample))) {
        throw std::runtime_error("Cannot read video frame caps.");
    }
    GstVideoFrame native_frame;
    if (!gst_video_frame_map(&native_frame, &video_info, gst_sample_get_buffer(sample),
                             GST_MAP_READ)) {
        throw std::runtime_error("Cannot map video frame.");
    }
    VideoFramePtr mapping(&native_frame);
    const Frame frame{
        static_cast<const std::uint8_t *>(GST_VIDEO_FRAME_PLANE_DATA(&native_frame, 0)),
        GST_VIDEO_FRAME_WIDTH(&native_frame), GST_VIDEO_FRAME_HEIGHT(&native_frame),
        GST_VIDEO_FRAME_PLANE_STRIDE(&native_frame, 0), format};
    on_frame(frame);
}

} // namespace streambrew::gst
