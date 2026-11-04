#include "pipeline/frame_pipeline.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_fixture(const char *path, bool with_audio) {
    streambrew::gst::CharPtr escaped(g_strescape(path, nullptr));
    std::string description = "videotestsrc pattern=snow num-buffers=60 ! "
                              "video/x-raw,format=BGR,width=32,height=24,framerate=30/1 ! "
                              "queue ! mux. avimux name=mux ! filesink location=\"" +
                              std::string(escaped.get()) + "\"";
    if (with_audio) {
        description += " audiotestsrc num-buffers=20 samplesperbuffer=800 ! "
                       "audio/x-raw,format=S16LE,rate=8000,channels=1 ! queue ! mux.";
    }

    GError *raw_error = nullptr;
    streambrew::gst::ObjectPtr<GstElement> pipeline(
        gst_parse_launch(description.c_str(), &raw_error));
    streambrew::gst::ErrorPtr error(raw_error);
    if (error != nullptr) {
        throw std::runtime_error(error->message);
    }
    require(pipeline != nullptr, "cannot build test fixture pipeline");
    streambrew::gst::ObjectPtr<GstBus> bus(gst_element_get_bus(pipeline.get()));
    const auto state = gst_element_set_state(pipeline.get(), GST_STATE_PLAYING);
    streambrew::gst::MessagePtr message(gst_bus_timed_pop_filtered(
        bus.get(), 5 * GST_SECOND,
        static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS)));
    gst_element_set_state(pipeline.get(), GST_STATE_NULL);
    require(state != GST_STATE_CHANGE_FAILURE && message != nullptr &&
                GST_MESSAGE_TYPE(message.get()) == GST_MESSAGE_EOS,
            "cannot generate test fixture (requires videotestsrc, avimux, and audio plugins)");
}

struct RunResult {
    std::vector<std::uint64_t> frames;
    Clock::duration elapsed;
};

RunResult run(const char *path, const streambrew::PipelineOptions &options,
              std::chrono::milliseconds processing_time = 0ms) {
    streambrew::FramePipeline pipeline(path, streambrew::PixelFormat::RGB, options);
    RunResult result;
    const auto start = Clock::now();
    pipeline.run([&](const streambrew::Frame &frame) {
        require(frame.data != nullptr && frame.width == 32 && frame.height == 24 &&
                    frame.stride >= 96 && frame.format == streambrew::PixelFormat::RGB,
                "unexpected frame layout");
        std::uint64_t hash = 14695981039346656037ull;
        for (int y = 0; y < frame.height; ++y) {
            for (int x = 0; x < frame.width * 3; ++x) {
                hash = (hash ^ frame.data[y * frame.stride + x]) * 1099511628211ull;
            }
        }
        result.frames.push_back(hash);
        std::this_thread::sleep_for(processing_time);
    });
    result.elapsed = Clock::now() - start;
    return result;
}

template <typename Action> void expect_invalid(Action action) {
    try {
        action();
    } catch (const std::invalid_argument &) {
        return;
    }
    throw std::runtime_error("expected std::invalid_argument");
}

void test_invalid_options(const char *path) {
    using namespace streambrew;
    PipelineOptions options;
    options.max_pending_frames = 0;
    expect_invalid([&] { FramePipeline pipeline(path, PixelFormat::RGB, options); });
    if (std::numeric_limits<std::size_t>::max() > G_MAXUINT) {
        options.max_pending_frames = std::numeric_limits<std::size_t>::max();
        expect_invalid([&] { FramePipeline pipeline(path, PixelFormat::RGB, options); });
    }
    options = {};
    options.timing = static_cast<DeliveryTiming>(-1);
    expect_invalid([&] { FramePipeline pipeline(path, PixelFormat::RGB, options); });
    options = {};
    options.overflow = static_cast<OverflowPolicy>(-1);
    expect_invalid([&] { FramePipeline pipeline(path, PixelFormat::RGB, options); });
    options = {};
    expect_invalid([&] { FramePipeline pipeline(path, PixelFormat::RGB, options, 0); });
    expect_invalid([&] { FramePipeline pipeline(path, PixelFormat::RGB, 0); });
}
} // namespace

int main(int argc, char **argv) {
    if (argc != 3) {
        return 2;
    }
    try {
        gst_init(nullptr, nullptr);
        write_fixture(argv[1], false);
        write_fixture(argv[2], true);
        test_invalid_options(argv[1]);

        streambrew::PipelineOptions fast;
        fast.timing = streambrew::DeliveryTiming::AsFastAsPossible;
        fast.max_pending_frames = 2;
        const auto baseline = run(argv[1], fast);
        require(baseline.frames.size() == 60, "fast mode must deliver all frames");

        const auto sequential = run(argv[1], fast, 5ms);
        require(sequential.frames == baseline.frames,
                "backpressure must preserve frame count and order for a slow consumer");

        const auto clocked = run(argv[1], streambrew::PipelineOptions{});
        require(clocked.frames == baseline.frames, "clocked mode must preserve frames");
        require(clocked.elapsed >= 1800ms, "clocked mode must wait for video timestamps");
        require(baseline.elapsed < clocked.elapsed / 2,
                "fast mode must avoid playback timing waits");

        const auto audio = run(argv[2], fast);
        require(audio.frames.size() == 60 && audio.elapsed < clocked.elapsed / 2,
                "audio playback must not pace fast mode");

        auto latest = fast;
        latest.max_pending_frames = 1;
        latest.overflow = streambrew::OverflowPolicy::DropOldest;
        const auto dropped = run(argv[1], latest, 100ms);
        require(!dropped.frames.empty() && dropped.frames.size() < baseline.frames.size(),
                "DropOldest must skip pending frames for a slow consumer");
        auto next = baseline.frames.begin();
        for (auto hash : dropped.frames) {
            next = std::find(next, baseline.frames.end(), hash);
            require(next != baseline.frames.end(), "retained frames must stay in order");
            ++next;
        }
        require(dropped.frames.back() == baseline.frames.back(),
                "EOS must drain the latest pending frame");

        std::cout << "Passed: timing, audio, backpressure, DropOldest, EOS, validation\n";
        std::cout << "Frames: fast=" << baseline.frames.size()
                  << " sequential=" << sequential.frames.size()
                  << " clocked=" << clocked.frames.size() << " DropOldest=" << dropped.frames.size()
                  << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
