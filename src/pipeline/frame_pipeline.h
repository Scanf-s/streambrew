#pragma once

#include <functional>
#include <string>

#include <gst/gst.h>

#include "gstreamer/gst_ptr.h"

namespace streambrew {
// Decodes a URI with playbin and passes each frame, 
// normalize video with fixed frame rate, and send it to a callback.
class FramePipeline {
public:
    using FrameCallback = std::function<void(GstSample *sample)>;

    // Throws std::runtime_error if the pipeline cannot be built.
    explicit FramePipeline(const std::string &uri, int fps = 30);
    ~FramePipeline();

    // A copy would stop and release the same pipeline a second time.
    FramePipeline(const FramePipeline &) = delete;
    FramePipeline &operator=(const FramePipeline &) = delete;

    // Plays until EOS or an error. Returns true on EOS and false on error.
    // The sample passed to on_frame is valid only during that call.
    bool run(const FrameCallback &on_frame);

private:
    enum class BusStatus { Running, Eos, Error };

    BusStatus poll_bus();

    // The destructor body runs before these members release their references,
    // so the pipeline is already in the NULL state at that point.
    gst::ObjectPtr<GstElement> pipeline_;
    gst::ObjectPtr<GstElement> appsink_;
    gst::ObjectPtr<GstBus> bus_;
};
} // namespace streambrew
