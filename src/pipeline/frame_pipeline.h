#pragma once

#include <atomic>
#include <functional>
#include <string>

#include <gst/gst.h>

#include "frame/frame.h"
#include "gstreamer/gst_ptr.h"
#include "pipeline/pipeline_options.h"
#include "pipeline/run_options.h"

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
    // Let run() finish (and join its thread) before destruction.
    ~FramePipeline();

    // A copy would stop and release the same pipeline a second time.
    FramePipeline(const FramePipeline &) = delete;
    FramePipeline &operator=(const FramePipeline &) = delete;

    // Runs once. Concurrent, recursive, and subsequent calls throw std::logic_error.
    // Invalid callbacks/options throw std::invalid_argument without consuming the run.
    // Frame data is valid only during the callback, on the calling thread.
    // All exits stop the pipeline before returning or propagating an exception.
    RunResult run(const FrameCallback &on_frame, const RunOptions &options = {});

    // Thread-safe and non-blocking; also allowed inside a callback or before run().
    // A request before run() cancels that run. The active callback is not interrupted.
    void request_stop() noexcept;
    RunState state() const noexcept;

  private:
    PixelFormat pixel_format_;
    std::atomic<RunState> run_state_{RunState::Ready};
    std::atomic<bool> stop_requested_{false};

    RunResult run_loop(const FrameCallback &on_frame, const RunOptions &options);

    // The destructor body runs before these members release their references,
    // so the pipeline is already in the NULL state at that point.
    gst::ObjectPtr<GstElement> pipeline_;
    gst::ObjectPtr<GstElement> appsink_;
    gst::ObjectPtr<GstBus> bus_;
};

} // namespace streambrew
