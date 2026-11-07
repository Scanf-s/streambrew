#include "gstreamer/frame_reader.h"
#include "pipeline/frame_pipeline.h"
#include "pipeline/pipeline_observer.h"

namespace streambrew {

RunResult FramePipeline::run_loop(const FrameCallback &on_frame, const RunOptions &options) {
    if (stop_requested_.load()) {
        return RunResult::Stopped;
    }

    // Install frame notifications before playback can produce the first sample.
    PipelineObserver observer(appsink_.get(), bus_.get(), options, stop_requested_);
    const auto startup = gst_element_set_state(pipeline_.get(), GST_STATE_PLAYING);
    observer.start(startup);

    // Bus events, stop requests, and deadlines are handled on the observer thread.
    while (auto sample = observer.next_frame()) {
        gst::deliver_frame(sample.get(), pixel_format_, on_frame);
        sample.reset();
        observer.frame_processed();
    }
    return observer.finish();
}

} // namespace streambrew
