#pragma once

#include <atomic>
#include <memory>
#include <thread>

#include "gstreamer/gst_ptr.h"
#include "pipeline/run_options.h"

namespace streambrew {
namespace detail {
struct ObserverState;
}

class PipelineObserver {
  public:
    PipelineObserver(GstElement *appsink, GstBus *bus, const RunOptions &options,
                     const std::atomic<bool> &stop_requested);
    ~PipelineObserver();
    PipelineObserver(const PipelineObserver &) = delete;
    PipelineObserver &operator=(const PipelineObserver &) = delete;

    void start(GstStateChangeReturn startup_result);

    // Worker-side operations. next_frame blocks on notifications, not polling.
    gst::SamplePtr next_frame();
    void frame_processed();

    // Joins the observer and forwards its error or normal completion reason.
    RunResult finish();

  private:
    void stop_observing() noexcept;
    static void observe(const std::shared_ptr<detail::ObserverState> &state) noexcept;

    GstElement *appsink_;
    std::shared_ptr<detail::ObserverState> state_;
    std::thread thread_;
};

} // namespace streambrew
