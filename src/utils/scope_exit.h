#pragma once

#include <type_traits>
#include <utility>

namespace streambrew::utils {

template <typename Action> class ScopeExit {
  public:
    explicit ScopeExit(Action action) noexcept(std::is_nothrow_move_constructible_v<Action>)
        : action_(std::move(action)) {
        static_assert(std::is_nothrow_invocable_v<Action &>, "cleanup must not throw");
    }
    ~ScopeExit() {
        if (active_) {
            action_();
        }
    }
    ScopeExit(const ScopeExit &) = delete;
    ScopeExit &operator=(const ScopeExit &) = delete;

    void release() noexcept { active_ = false; }

  private:
    Action action_;
    bool active_ = true;
};

} // namespace streambrew::utils
