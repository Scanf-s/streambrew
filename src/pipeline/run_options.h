#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

namespace streambrew {

enum class RunState { Ready, Running, Finished };
enum class RunResult { EndOfStream, Stopped };
enum class TimeoutStage { FirstFrame, NextFrame };
enum class TimeoutAction { WarnAndContinue, Throw };

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
    // Timeouts are disabled by default.
    // Configured values must be positive.

    // Starts before the PLAYING transition and ends at the first usable frame.
    std::optional<std::chrono::milliseconds> first_frame_timeout;
    // Starts after each callback and frame cleanup. Callback time is excluded.
    std::optional<std::chrono::milliseconds> frame_wait_timeout;
    TimeoutAction timeout_action = TimeoutAction::Throw;
    // Runs on the observer thread. Empty means log to std::clog.
    // Exceptions from this handler are forwarded to the run() caller.
    std::function<void(const std::string &)> warning_handler;
};

} // namespace streambrew
