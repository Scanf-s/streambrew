#include "pipeline/observer_state.h"
#include "pipeline/pipeline_observer.h"

#include <gst/app/gstappsink.h>

namespace streambrew {
namespace detail {
namespace {
using StateHandle = std::shared_ptr<ObserverState>;

GstFlowReturn frame_available(GstAppSink *, gpointer data) noexcept {
    const auto &state = *static_cast<StateHandle *>(data);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        ++state->frame_revision;
        state->awaiting_frame = false;
        state->warning_issued = false;
    }
    state->notify();
    return GST_FLOW_OK;
}

void input_eos(GstAppSink *, gpointer data) noexcept {
    const auto &state = *static_cast<StateHandle *>(data);
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->input_eos = true;
    }
    state->notify();
}

void release_state(gpointer data) { delete static_cast<StateHandle *>(data); }
} // namespace

void attach_frame_notifications(GstElement *appsink, const StateHandle &state) {
    GstAppSinkCallbacks callbacks{};
    callbacks.new_sample = frame_available;
    callbacks.eos = input_eos;
    // GStreamer retains this handle while a callback is in flight.
    gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &callbacks, new StateHandle(state),
                               release_state);
}

void detach_frame_notifications(GstElement *appsink) noexcept {
    // GStreamer 1.24 requires a callbacks struct, rather than nullptr.
    GstAppSinkCallbacks callbacks{};
    gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &callbacks, nullptr, nullptr);
}
} // namespace detail

gst::SamplePtr PipelineObserver::next_frame() {
    auto &state = *state_;
    while (true) {
        std::unique_lock<std::mutex> lock(state.mutex);
        if (state.failure) {
            std::rethrow_exception(state.failure);
        }
        if (state.result) {
            return {};
        }
        if (state.stop_requested.load()) {
            // Let the observer classify the stop, including any queued bus error.
            state.changed.wait(lock, [&] { return state.failure || state.result.has_value(); });
            continue;
        }
        const auto revision = state.frame_revision;
        lock.unlock();

        // Always drain available samples before sleeping, even after EOS.
        gst::SamplePtr sample(gst_app_sink_try_pull_sample(GST_APP_SINK(appsink_), 0));

        lock.lock();
        if (state.failure) {
            std::rethrow_exception(state.failure);
        }
        if (state.result) {
            return {};
        }
        if (state.stop_requested.load()) {
            continue;
        }
        if (sample) {
            state.awaiting_frame = false;
            lock.unlock();
            gst::wake_bus(state.bus.get());
            return sample;
        }
        if (state.eos_received) {
            state.result = RunResult::EndOfStream;
            return {};
        }
        if (state.frame_revision != revision) {
            continue;
        }

        state.awaiting_frame = true;
        lock.unlock();
        gst::wake_bus(state.bus.get());
        lock.lock();
        state.changed.wait(lock, [&] {
            return state.frame_revision != revision || state.failure || state.result.has_value() ||
                   state.eos_received || state.stop_requested.load();
        });
    }
}

void PipelineObserver::frame_processed() {
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->awaiting_frame = false;
        state_->warning_issued = false;
        state_->stage = TimeoutStage::NextFrame;
        state_->budget.reset(state_->options.frame_wait_timeout);
    }
    state_->notify();
}

} // namespace streambrew
