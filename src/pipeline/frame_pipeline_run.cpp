#include "gstreamer/bus.h"
#include "gstreamer/frame_reader.h"
#include "pipeline/frame_pipeline.h"
#include "utils/wait_budget.h"

#include <algorithm>
#include <gst/app/gstappsink.h>

namespace streambrew {
namespace {
GstClockTime next_wait(const utils::WaitBudget &budget, TimeoutStage stage) {
    using Milliseconds = std::chrono::milliseconds;
    const auto remaining = budget.remaining();
    if (remaining && *remaining == Milliseconds::zero()) {
        throw PipelineTimeout(stage);
    }
    // Only the bounded polling interval is converted, avoiding overflow even
    // when the configured timeout is the largest millisecond duration.
    const auto wait = std::min(Milliseconds(100), remaining.value_or(Milliseconds(100)));
    return static_cast<GstClockTime>(wait.count()) * GST_MSECOND;
}
} // namespace

RunResult FramePipeline::run_loop(const FrameCallback &on_frame, const RunOptions &options) {
    if (stop_requested_.load()) {
        return RunResult::Stopped;
    }
    utils::WaitBudget budget(options.first_frame_timeout);
    auto stage = TimeoutStage::FirstFrame;
    if (gst_element_set_state(pipeline_.get(), GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        gst::poll_bus(bus_.get());
        throw std::runtime_error("Could not start the pipeline.");
    }

    bool eos_received = false;
    while (true) {
        // Read errors before reporting stop/EOS or deciding that a wait expired.
        eos_received = gst::poll_bus(bus_.get()) == gst::BusStatus::Eos || eos_received;
        if (stop_requested_.load()) {
            return RunResult::Stopped;
        }
        const auto wait = eos_received ? 0 : next_wait(budget, stage);

        if (!gst::sink_started(appsink_.get())) {
            // is_eos() is also true before a sink starts. Wait on the bus to
            // avoid both premature EOS and spinning during startup.
            if (eos_received) {
                return RunResult::EndOfStream;
            }
            eos_received = gst::poll_bus(bus_.get(), wait) == gst::BusStatus::Eos;
            continue;
        }

        gst::SamplePtr sample(gst_app_sink_try_pull_sample(GST_APP_SINK(appsink_.get()), wait));
        eos_received = gst::poll_bus(bus_.get()) == gst::BusStatus::Eos || eos_received;
        if (stop_requested_.load()) {
            return RunResult::Stopped;
        }
        if (sample == nullptr) {
            if (eos_received || (gst::sink_started(appsink_.get()) &&
                                 gst_app_sink_is_eos(GST_APP_SINK(appsink_.get())))) {
                return RunResult::EndOfStream;
            }
            continue;
        }

        gst::deliver_frame(sample.get(), pixel_format_, on_frame);
        sample.reset();
        budget.reset(options.frame_wait_timeout);
        stage = TimeoutStage::NextFrame;
    }
}

} // namespace streambrew
