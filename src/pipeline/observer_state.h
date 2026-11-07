#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>

#include "gstreamer/bus.h"
#include "pipeline/run_options.h"
#include "utils/wait_budget.h"

namespace streambrew::detail {

// The observer decides stop, error, and timeout results. The worker decides
// EndOfStream because only it knows whether queued samples remain.
// Every mutable field below is protected by mutex, except stop_requested.
struct ObserverState {
    ObserverState(GstBus *native_bus, const RunOptions &run_options, const std::atomic<bool> &stop)
        : bus(GST_BUS(gst_object_ref(native_bus))), options(run_options), stop_requested(stop),
          budget(options.first_frame_timeout) {}

    void notify() noexcept {
        changed.notify_all();
        gst::wake_bus(bus.get());
    }

    gst::ObjectPtr<GstBus> bus;
    const RunOptions options;
    const std::atomic<bool> &stop_requested;
    std::mutex mutex;
    std::condition_variable changed;
    std::uint64_t frame_revision = 0;
    bool awaiting_frame = true;
    bool input_eos = false;
    bool eos_received = false;
    bool finish_requested = false;
    bool startup_failed = false;
    bool warning_issued = false;
    TimeoutStage stage = TimeoutStage::FirstFrame;
    utils::WaitBudget budget;
    std::optional<RunResult> result;
    std::exception_ptr failure;
};

void attach_frame_notifications(GstElement *appsink, const std::shared_ptr<ObserverState> &state);
void detach_frame_notifications(GstElement *appsink) noexcept;

} // namespace streambrew::detail
