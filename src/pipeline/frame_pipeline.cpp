#include "pipeline/frame_pipeline.h"
#include "gstreamer/pipeline_builder.h"
#include "utils/scope_exit.h"

#include <stdexcept>
#include <utility>

namespace streambrew {
namespace {
void validate_run_options(const RunOptions &options) {
    if ((options.first_frame_timeout && options.first_frame_timeout->count() <= 0) ||
        (options.frame_wait_timeout && options.frame_wait_timeout->count() <= 0)) {
        throw std::invalid_argument("frame wait timeouts must be positive when set");
    }
}
} // namespace

FramePipeline::FramePipeline(const std::string &source, PixelFormat format, int fps)
    : FramePipeline(source, format, PipelineOptions{}, fps) {}

FramePipeline::FramePipeline(const std::string &source, PixelFormat format,
                             const PipelineOptions &options, int fps)
    : pixel_format_(format) {
    auto elements = gst::build_frame_pipeline(source, format, options, fps);
    pipeline_ = std::move(elements.pipeline);
    appsink_ = std::move(elements.appsink);
    bus_ = std::move(elements.bus);
}

FramePipeline::~FramePipeline() { gst_element_set_state(pipeline_.get(), GST_STATE_NULL); }

void FramePipeline::request_stop() noexcept { stop_requested_.store(true); }

RunState FramePipeline::state() const noexcept { return run_state_.load(); }

RunResult FramePipeline::run(const FrameCallback &on_frame, const RunOptions &options) {
    if (!on_frame) {
        throw std::invalid_argument("frame callback must not be empty");
    }
    validate_run_options(options);
    RunState expected = RunState::Ready;
    if (!run_state_.compare_exchange_strong(expected, RunState::Running)) {
        throw std::logic_error("FramePipeline::run() can only be called once");
    }

    const auto stop = [this]() noexcept {
        const auto result = gst_element_set_state(pipeline_.get(), GST_STATE_NULL);
        run_state_.store(RunState::Finished);
        return result;
    };
    utils::ScopeExit cleanup([&stop]() noexcept { stop(); });
    const auto result = run_loop(on_frame, options);
    const auto stopped = stop();
    cleanup.release();
    if (stopped == GST_STATE_CHANGE_FAILURE) {
        throw std::runtime_error("Could not stop the pipeline.");
    }
    return result;
}

} // namespace streambrew
