#pragma once

#include <functional>
#include <string>

#include <gst/gst.h>

#include "frame/frame.h"
#include "gstreamer/gst_ptr.h"
#include "pipeline/pipeline_options.h"

namespace streambrew {

// Decodes a file or URI, normalizes video to a fixed frame rate, and delivers
// frames according to independently configured timing and overflow policies.
class FramePipeline {
  public:
    using FrameCallback = std::function<void(const Frame &frame)>;

    // Uses the default PipelineOptions. Throws std::invalid_argument for invalid
    // options, format, or fps, and std::runtime_error if construction fails.
    explicit FramePipeline(const std::string &source, PixelFormat format, int fps = 30);
    FramePipeline(const std::string &source, PixelFormat format, const PipelineOptions &options,
                  int fps = 30);
    ~FramePipeline();

    // A copy would stop and release the same pipeline a second time.
    FramePipeline(const FramePipeline &) = delete;
    FramePipeline &operator=(const FramePipeline &) = delete;

    // Runs until EOS or an error
    void run(const FrameCallback &on_frame);

  private:
    PixelFormat pixel_format_;

    // The destructor body runs before these members release their references,
    // so the pipeline is already in the NULL state at that point.
    gst::ObjectPtr<GstElement> pipeline_;
    gst::ObjectPtr<GstElement> appsink_;
    gst::ObjectPtr<GstBus> bus_;
};

} // namespace streambrew
