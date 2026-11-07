#include "pipeline/pipeline_observer.h"
#include "pipeline/observer_state.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace streambrew {
namespace {
using detail::ObserverState;

void report_warning(const ObserverState &state, const std::string &message) {
    if (state.options.warning_handler) {
        state.options.warning_handler(message);
    } else {
        std::clog << message << '\n';
    }
}

void read_bus(ObserverState &state, GstClockTime timeout) {
    auto message = gst::wait_for_bus_message(state.bus.get(), timeout);
    if (!message && timeout == GST_CLOCK_TIME_NONE) {
        throw std::runtime_error("GStreamer bus stopped while observing the pipeline.");
    }
    while (message) {
        switch (GST_MESSAGE_TYPE(message.get())) {
        case GST_MESSAGE_ERROR:
            throw std::runtime_error(gst::describe_bus_message(message.get()));
        case GST_MESSAGE_WARNING:
            report_warning(state, gst::describe_bus_message(message.get()));
            break;
        case GST_MESSAGE_EOS: {
            std::lock_guard<std::mutex> lock(state.mutex);
            state.eos_received = true;
            break;
        }
        default:
            break;
        }
        message = gst::wait_for_bus_message(state.bus.get(), 0);
    }
}

// Called with state.mutex held. No periodic polling interval is needed.
// The control/frame messages wake the bus and otherwise wait until the deadline.
GstClockTime observation_wait(ObserverState &state, std::string &warning) {
    if (!state.awaiting_frame || state.eos_received || state.warning_issued) {
        return GST_CLOCK_TIME_NONE;
    }
    const auto remaining = state.budget.remaining();
    if (!remaining) {
        return GST_CLOCK_TIME_NONE;
    }
    if (*remaining == std::chrono::milliseconds::zero()) {
        if (state.options.timeout_action == TimeoutAction::Throw) {
            throw PipelineTimeout(state.stage);
        }
        state.warning_issued = true;
        warning = std::string("Warning: ") + PipelineTimeout(state.stage).what();
        return GST_CLOCK_TIME_NONE;
    }

    // Saturate very large millisecond durations before converting to nanoseconds.
    const auto milliseconds = static_cast<GstClockTime>(remaining->count());
    constexpr auto maximum = GST_CLOCK_TIME_NONE - 1;
    if (milliseconds > maximum / GST_MSECOND) {
        return maximum;
    }
    return milliseconds * GST_MSECOND;
}
} // namespace

PipelineObserver::PipelineObserver(GstElement *appsink, GstBus *bus, const RunOptions &options,
                                   const std::atomic<bool> &stop_requested)
    : appsink_(appsink),
      state_(std::make_shared<detail::ObserverState>(bus, options, stop_requested)) {
    detail::attach_frame_notifications(appsink_, state_);
}

PipelineObserver::~PipelineObserver() {
    stop_observing();
    detail::detach_frame_notifications(appsink_);
}

void PipelineObserver::start(GstStateChangeReturn startup_result) {
    state_->startup_failed = startup_result == GST_STATE_CHANGE_FAILURE;
    thread_ = std::thread(observe, state_);
}

void PipelineObserver::stop_observing() noexcept {
    if (!thread_.joinable()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->finish_requested = true;
    }
    state_->notify();
    thread_.join();
}

RunResult PipelineObserver::finish() {
    stop_observing();
    auto &state = *state_;
    std::lock_guard<std::mutex> lock(state.mutex);
    if (state.failure) {
        std::rethrow_exception(state.failure);
    }
    if (!state.result) {
        throw std::logic_error("Observer finished without a run result.");
    }
    return *state.result;
}

void PipelineObserver::observe(const std::shared_ptr<detail::ObserverState> &state) noexcept {
    try {
        GstClockTime wait = 0;
        while (true) {
            read_bus(*state, wait);

            bool finish = false;
            bool stop = false;
            std::string warning;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->startup_failed) {
                    throw std::runtime_error("Could not start the pipeline.");
                }
                finish = state->finish_requested;
                stop = state->stop_requested.load();
                state->eos_received = state->eos_received || state->input_eos;
                if (!finish && !stop) {
                    wait = observation_wait(*state, warning);
                }
            }
            state->changed.notify_all();

            if (finish || stop) {
                // Acknowledge shutdown only after a final drain. 
                // This preserves errors queued by the last callback even if EOS arrived first.
                read_bus(*state, 0);
                if (stop) {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    state->result = RunResult::Stopped;
                }
                state->changed.notify_all();
                return;
            }
            if (!warning.empty()) {
                report_warning(*state, warning);
            }
        }
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->failure = std::current_exception();
        }
        state->changed.notify_all();
    }
}

} // namespace streambrew
