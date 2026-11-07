#include "pipeline/frame_pipeline.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace streambrew;

// CTest reports this exit code as a skipped test.
constexpr int skip_exit_code = 77;

void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// playbin only selects decoders whose rank is at least GST_RANK_MARGINAL.
bool has_h264_decoder() {
    GList *decoders = gst_element_factory_list_get_elements(
        GST_ELEMENT_FACTORY_TYPE_DECODER | GST_ELEMENT_FACTORY_TYPE_MEDIA_VIDEO, GST_RANK_MARGINAL);
    GstCaps *caps = gst_caps_from_string("video/x-h264");
    GList *usable = gst_element_factory_list_filter(decoders, caps, GST_PAD_SINK, FALSE);
    const bool found = usable != nullptr;
    gst_plugin_feature_list_free(usable);
    gst_plugin_feature_list_free(decoders);
    gst_caps_unref(caps);
    return found;
}

// Hashing every 8th pixel of every 8th row keeps HD frames fast to compare.
std::uint64_t hash_frame(const Frame &frame) {
    std::uint64_t hash = 14695981039346656037ull;
    for (int y = 0; y < frame.height; y += 8) {
        const std::uint8_t *row = frame.data + static_cast<std::ptrdiff_t>(y) * frame.stride;
        for (int x = 0; x < frame.width; x += 8) {
            for (int channel = 0; channel < 3; ++channel) {
                hash = (hash ^ row[x * 3 + channel]) * 1099511628211ull;
            }
        }
    }
    return hash;
}

std::vector<std::uint64_t> run(const char *path, FrameRateMode mode, int fps) {
    PipelineOptions options;
    options.timing = DeliveryTiming::AsFastAsPossible;
    options.frame_rate = mode;
    options.fps = fps;
    FramePipeline pipeline(path, PixelFormat::RGB, options);
    std::vector<std::uint64_t> hashes;
    const auto result =
        pipeline.run([&](const Frame &frame) { hashes.push_back(hash_frame(frame)); });
    require(result == RunResult::EndOfStream, "the sample must play until the end");
    return hashes;
}

// A static scene can repeat a frame in the source, so compare runs without repeats.
std::vector<std::uint64_t> without_repeats(std::vector<std::uint64_t> hashes) {
    hashes.erase(std::unique(hashes.begin(), hashes.end()), hashes.end());
    return hashes;
}

} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        return 2;
    }
    const char *path = argv[1];
    gst_init(nullptr, nullptr);
    if (!std::filesystem::exists(path)) {
        std::cout << "Skipped: " << path
                  << " does not exist. See the Tests section in README.md.\n";
        return skip_exit_code;
    }
    if (!has_h264_decoder()) {
        std::cout << "Skipped: no H.264 decoder is installed, for example gst-libav.\n";
        return skip_exit_code;
    }

    try {
        // The checks below assume that the sample runs between 12 and 60 fps.
        const auto source = run(path, FrameRateMode::Source, 30);
        require(!source.empty(), "Source must deliver frames");

        // True when the frames are source frames in source order, with some frames dropped.
        const auto only_drops_source_frames = [&source](const std::vector<std::uint64_t> &frames) {
            auto next = source.begin();
            for (const auto hash : frames) {
                next = std::find(next, source.end(), hash);
                if (next == source.end()) {
                    return false;
                }
                ++next;
            }
            return true;
        };

        require(run(path, FrameRateMode::Limit, 60) == source,
                "Limit must not repeat frames when the source is below fps");

        const auto limited = run(path, FrameRateMode::Limit, 12);
        require(!limited.empty() && limited.size() < source.size() &&
                    only_drops_source_frames(limited),
                "Limit must only drop frames when the source exceeds fps");

        const auto raised = run(path, FrameRateMode::Fixed, 60);
        require(raised.size() > source.size() && without_repeats(raised) == without_repeats(source),
                "Fixed must repeat source frames when fps exceeds the source rate");

        const auto lowered = run(path, FrameRateMode::Fixed, 12);
        require(!lowered.empty() && lowered.size() < source.size() &&
                    only_drops_source_frames(lowered),
                "Fixed must drop source frames when fps is below the source rate");

        std::cout << "Passed: Source, Limit, and Fixed frame rate modes\n";
        std::cout << "Frames: Source=" << source.size() << " Limit12=" << limited.size()
                  << " Fixed60=" << raised.size() << " Fixed12=" << lowered.size() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
