#pragma once

#include <functional>
#include <string>

#include <gst/gst.h>

#include "frame/frame.h"
#include "gstreamer/gst_ptr.h"

namespace streambrew {
// Decodes a URI with playbin and passes each frame, 
// normalize video with fixed frame rate, and send it to a callback.
class FramePipeline {
public:
    using FrameCallback = std::function<void(const Frame &frame)>;

    // Throws std::runtime_error if the pipeline cannot be built or invalid arguments are passed
    explicit FramePipeline(const std::string &source, PixelFormat format, int fps = 30);
    ~FramePipeline();

    // A copy would stop and release the same pipeline a second time.
    FramePipeline(const FramePipeline &) = delete;
    FramePipeline &operator=(const FramePipeline &) = delete;

    // Plays until EOS or an error.
    void run(const FrameCallback &on_frame);

private:

    PixelFormat pixel_format_;

    enum class BusStatus { Running, Eos };

    BusStatus poll_bus();

    // The destructor body runs before these members release their references,
    // so the pipeline is already in the NULL state at that point.
    gst::ObjectPtr<GstElement> pipeline_;
    gst::ObjectPtr<GstElement> appsink_;
    gst::ObjectPtr<GstBus> bus_;
};
} // namespace streambrew
