#pragma once

#include <chrono>
#include <optional>
#include <stdexcept>

namespace streambrew {

enum class RunState { Ready, Running, Finished };
enum class RunResult { EndOfStream, Stopped };
enum class TimeoutStage { FirstFrame, NextFrame };

class PipelineTimeout : public std::runtime_error {
  public:
    explicit PipelineTimeout(TimeoutStage stage)
        : std::runtime_error(stage == TimeoutStage::FirstFrame
                                 ? "Timed out waiting for first frame."
                                 : "Timed out waiting for next frame."),
          stage_(stage) {}

    TimeoutStage stage() const noexcept { return stage_; }

  private:
    TimeoutStage stage_;
};

struct RunOptions {
    // Disabled by default; configured values must be positive.
    // Starts before the PLAYING transition and ends at the first usable frame.
    std::optional<std::chrono::milliseconds> first_frame_timeout;
    // Starts after each callback and frame cleanup. Callback time is excluded.
    std::optional<std::chrono::milliseconds> frame_wait_timeout;
};

} // namespace streambrew
