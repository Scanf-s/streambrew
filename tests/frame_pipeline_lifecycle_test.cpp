#include "pipeline/frame_pipeline.h"

#include <gst/app/gstappsrc.h>

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace streambrew {
struct FramePipelineTestAccess {
    static GstElement *native(const FramePipeline &pipeline) { return pipeline.pipeline_.get(); }
};
} // namespace streambrew

namespace {
using namespace streambrew;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Error, typename Action> void expect_throw(Action action) {
    try {
        action();
    } catch (const Error &) {
        return;
    }
    throw std::runtime_error("expected exception was not thrown");
}

enum class InputMode { Finite, Silent, Stall };

struct SyntheticInput {
    InputMode mode;
    guint limit;
    guint generated = 0;
    std::atomic<bool> started{false};

    static void need_data(GstAppSrc *source, guint, gpointer data) {
        auto &input = *static_cast<SyntheticInput *>(data);
        if (input.mode == InputMode::Silent) {
            return;
        }
        if (input.generated == input.limit) {
            if (input.mode == InputMode::Finite) {
                gst_app_src_end_of_stream(source);
            }
            return;
        }
        constexpr gsize size = gsize{32} * 24 * 3;
        GstBuffer *buffer = gst_buffer_new_allocate(nullptr, size, nullptr);
        gst_buffer_memset(buffer, 0, static_cast<guint8>(input.generated), size);
        GST_BUFFER_PTS(buffer) = gst_util_uint64_scale(input.generated, GST_SECOND, 30);
        GST_BUFFER_DURATION(buffer) = GST_SECOND / 30;
        ++input.generated;
        gst_app_src_push_buffer(source, buffer);
    }

    static void setup(GstElement *, GstElement *source, gpointer data) {
        auto &input = *static_cast<SyntheticInput *>(data);
        GstCaps *caps =
            gst_caps_from_string("video/x-raw,format=RGB,width=32,height=24,framerate=30/1");
        gst_app_src_set_caps(GST_APP_SRC(source), caps);
        gst_caps_unref(caps);
        g_object_set(source, "is-live", TRUE, "format", GST_FORMAT_TIME, nullptr);
        GstAppSrcCallbacks callbacks{};
        callbacks.need_data = need_data;
        gst_app_src_set_callbacks(GST_APP_SRC(source), &callbacks, data, nullptr);
        input.started.store(true);
    }
};

PipelineOptions fast_options() {
    PipelineOptions options;
    options.timing = DeliveryTiming::AsFastAsPossible;
    return options;
}

struct Fixture {
    // Outlives the pipeline and its streaming callbacks.
    SyntheticInput input;
    FramePipeline pipeline;

    explicit Fixture(InputMode mode = InputMode::Finite, guint frames = 12)
        : input{mode, frames}, pipeline("appsrc://", PixelFormat::RGB, fast_options()) {
        g_signal_connect(FramePipelineTestAccess::native(pipeline), "source-setup",
                         G_CALLBACK(SyntheticInput::setup), &input);
    }
};

void require_stopped(const FramePipeline &pipeline) {
    require(pipeline.state() == RunState::Finished, "run state must be Finished after teardown");
    GstState native = GST_STATE_VOID_PENDING;
    const auto result =
        gst_element_get_state(FramePipelineTestAccess::native(pipeline), &native, nullptr, 0);
    require(result == GST_STATE_CHANGE_SUCCESS && native == GST_STATE_NULL,
            "native pipeline must reach NULL while its owner is still alive");
}

void test_eos_and_reentry() {
    Fixture fixture;
    require(fixture.pipeline.state() == RunState::Ready, "new pipeline must be Ready");
    int frames = 0;
    const auto result = fixture.pipeline.run([&](const Frame &) {
        if (++frames == 1) {
            expect_throw<std::logic_error>([&] { fixture.pipeline.run([](const Frame &) {}); });
        }
    });
    require(result == RunResult::EndOfStream && frames == 12,
            "normal EOS must drain all frames, including the last one");
    require_stopped(fixture.pipeline);
    expect_throw<std::logic_error>([&] { fixture.pipeline.run([](const Frame &) {}); });
}

void test_callback_stop() {
    Fixture fixture;
    int frames = 0;
    const auto result = fixture.pipeline.run([&](const Frame &) {
        ++frames;
        fixture.pipeline.request_stop();
        fixture.pipeline.request_stop();
    });
    require(result == RunResult::Stopped && frames == 1,
            "callback stop must discard remaining frames");
    require_stopped(fixture.pipeline);
}

void test_stop_before_run() {
    FramePipeline pipeline("/streambrew-no-such-input.avi", PixelFormat::RGB, fast_options());
    pipeline.request_stop();
    require(pipeline.state() == RunState::Ready, "request_stop must not start the pipeline");
    int frames = 0;
    const auto result = pipeline.run([&](const Frame &) { ++frames; });
    require(result == RunResult::Stopped && frames == 0,
            "a stop before run must cancel without opening the source");
    pipeline.request_stop();
    require_stopped(pipeline);
}

void test_exception_cleanup() {
    struct CallbackError : std::runtime_error {
        using std::runtime_error::runtime_error;
    };
    Fixture fixture;
    expect_throw<CallbackError>([&] {
        fixture.pipeline.run([](const Frame &) { throw CallbackError("original callback error"); });
    });
    require_stopped(fixture.pipeline);
    expect_throw<std::logic_error>([&] { fixture.pipeline.run([](const Frame &) {}); });

    FramePipeline missing("/streambrew-no-such-input.avi", PixelFormat::RGB, fast_options());
    expect_throw<std::runtime_error>([&] { missing.run([](const Frame &) {}); });
    require_stopped(missing);
}

void test_validation() {
    Fixture fixture;
    expect_throw<std::invalid_argument>([&] { fixture.pipeline.run({}); });
    RunOptions options;
    options.first_frame_timeout = 0ms;
    expect_throw<std::invalid_argument>(
        [&] { fixture.pipeline.run([](const Frame &) {}, options); });
    options.first_frame_timeout = -1ms;
    expect_throw<std::invalid_argument>(
        [&] { fixture.pipeline.run([](const Frame &) {}, options); });
    options = {};
    options.frame_wait_timeout = 0ms;
    expect_throw<std::invalid_argument>(
        [&] { fixture.pipeline.run([](const Frame &) {}, options); });
    options.frame_wait_timeout = -1ms;
    expect_throw<std::invalid_argument>(
        [&] { fixture.pipeline.run([](const Frame &) {}, options); });
    require(fixture.pipeline.state() == RunState::Ready, "validation must not consume the run");
    fixture.pipeline.request_stop();
    require(fixture.pipeline.run([](const Frame &) {}) == RunResult::Stopped,
            "a valid run must remain available after validation errors");
}

void test_timeouts() {
    {
        Fixture fixture(InputMode::Silent);
        RunOptions options;
        options.first_frame_timeout = 60ms;
        const auto start = Clock::now();
        bool timed_out = false;
        try {
            fixture.pipeline.run([](const Frame &) {}, options);
        } catch (const PipelineTimeout &error) {
            timed_out = error.stage() == TimeoutStage::FirstFrame;
        }
        require(timed_out && Clock::now() - start >= 60ms,
                "silent input must time out in the first-frame stage, not report EOS");
        require_stopped(fixture.pipeline);
    }
    {
        Fixture fixture(InputMode::Stall, 3);
        RunOptions options;
        options.first_frame_timeout = 2s;
        options.frame_wait_timeout = 60ms;
        int frames = 0;
        bool timed_out = false;
        try {
            fixture.pipeline.run([&](const Frame &) { ++frames; }, options);
        } catch (const PipelineTimeout &error) {
            timed_out = error.stage() == TimeoutStage::NextFrame;
        }
        require(timed_out && frames > 0, "stalled input must time out after delivering frames");
        require_stopped(fixture.pipeline);
    }
    {
        Fixture fixture(InputMode::Finite, 4);
        RunOptions options;
        options.first_frame_timeout = 2s;
        options.frame_wait_timeout = 50ms;
        int frames = 0;
        const auto result = fixture.pipeline.run(
            [&](const Frame &) {
                ++frames;
                std::this_thread::sleep_for(100ms);
            },
            options);
        require(result == RunResult::EndOfStream && frames == 4,
                "slow callbacks must not consume the next-frame waiting budget");
        require_stopped(fixture.pipeline);
    }
}

void test_stop_while_waiting() {
    Fixture fixture(InputMode::Silent);
    auto worker =
        std::async(std::launch::async, [&] { return fixture.pipeline.run([](const Frame &) {}); });
    const auto deadline = Clock::now() + 2s;
    while (!fixture.input.started.load() && Clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    const bool started = fixture.input.started.load();
    const bool running = fixture.pipeline.state() == RunState::Running;
    fixture.pipeline.request_stop();
    const auto result = worker.get();
    require(started && running && result == RunResult::Stopped,
            "external stop must end an input wait with disabled timeouts");
    require_stopped(fixture.pipeline);
}

void test_stop_during_callback_and_concurrent_run() {
    Fixture fixture(InputMode::Finite, 60);
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::promise<void> release;
    auto release_future = release.get_future();
    int frames = 0;
    auto worker = std::async(std::launch::async, [&] {
        return fixture.pipeline.run([&](const Frame &frame) {
            ++frames;
            const auto value = frame.data[0];
            entered.set_value();
            release_future.wait();
            require(frame.data[0] == value, "stop must preserve the active callback's mapping");
        });
    });
    const bool callback_entered = entered_future.wait_for(2s) == std::future_status::ready;
    auto concurrent = std::async(std::launch::async, [&] {
        try {
            fixture.pipeline.run([](const Frame &) {});
        } catch (const std::logic_error &) {
            return true;
        }
        return false;
    });
    const bool rejected_promptly = concurrent.wait_for(500ms) == std::future_status::ready;
    fixture.pipeline.request_stop();
    const bool callback_still_active = worker.wait_for(0ms) == std::future_status::timeout;
    release.set_value();
    const auto result = worker.get();
    const bool rejected = concurrent.get();
    require(callback_entered && rejected_promptly && rejected && callback_still_active &&
                result == RunResult::Stopped && frames == 1,
            "concurrent run must be rejected and stop must wait for the active callback");
    require_stopped(fixture.pipeline);
}

void test_bus_error_before_eos_result() {
    Fixture fixture;
    bool injected = false;
    bool received_error = false;
    try {
        fixture.pipeline.run([&](const Frame &) {
            if (!injected) {
                injected = true;
                auto *native = FramePipelineTestAccess::native(fixture.pipeline);
                gst_element_post_message(native, gst_message_new_eos(GST_OBJECT(native)));
                gst::ErrorPtr error(
                    g_error_new_literal(GST_CORE_ERROR, GST_CORE_ERROR_FAILED, "injected failure"));
                gst_element_post_message(
                    native, gst_message_new_error(GST_OBJECT(native), error.get(), nullptr));
            }
        });
    } catch (const std::runtime_error &error) {
        received_error = std::string(error.what()).find("injected failure") != std::string::npos;
    }
    require(received_error, "an already queued bus error must not be hidden by EOS");
    require_stopped(fixture.pipeline);
}
} // namespace

int main() {
    try {
        test_eos_and_reentry();
        test_callback_stop();
        test_stop_before_run();
        test_exception_cleanup();
        test_validation();
        test_timeouts();
        test_stop_while_waiting();
        test_stop_during_callback_and_concurrent_run();
        test_bus_error_before_eos_result();
        std::cout << "Passed: run state, stop, teardown, timeouts, concurrency, EOS, errors\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
