#pragma once

#include <chrono>
#include <optional>

namespace streambrew::utils {

class WaitBudget {
  public:
    using Duration = std::chrono::milliseconds;

    explicit WaitBudget(std::optional<Duration> timeout) noexcept : timeout_(timeout) {}

    void reset(std::optional<Duration> timeout) noexcept {
        timeout_ = timeout;
        start_ = Clock::now();
    }

    std::optional<Duration> remaining() const noexcept {
        if (!timeout_) {
            // nullopt means unlimited
            // zero means the budget has expired.
            return std::nullopt;
        }
        const auto elapsed = std::chrono::duration_cast<Duration>(Clock::now() - start_);
        return elapsed >= *timeout_ ? Duration::zero() : *timeout_ - elapsed;
    }

  private:
    using Clock = std::chrono::steady_clock;
    std::optional<Duration> timeout_;
    Clock::time_point start_ = Clock::now();
};

} // namespace streambrew::utils
