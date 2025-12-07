#include <atomic>
#include <coroutine>
#include <optional>
#include <stop_token>

namespace coral {

namespace detail {
class stop_token_awaiter {
public:
    explicit stop_token_awaiter(std::stop_token stop_token)
        : stop_token_(stop_token)
    {
    }

    bool await_ready() const noexcept { return stop_token_.stop_requested(); }

    bool await_suspend(std::coroutine_handle<> continuation) noexcept
    {
        callback_.emplace(stop_token_, stop_callback{this, continuation});
        const auto c = race_flag_.fetch_add(1, std::memory_order::acq_rel);
        return c == 0;
    }

    constexpr void await_resume() const noexcept {}

private:
    struct stop_callback {
        stop_token_awaiter* owner;
        std::coroutine_handle<> continuation;

        void operator()() const noexcept
        {
            const auto c = owner->race_flag_.fetch_add(1, std::memory_order::acq_rel);
            if (c == 1) {
                continuation();
            }
        }
    };
    std::stop_token stop_token_;
    std::atomic<int> race_flag_{0};
    std::optional<std::stop_callback<stop_callback>> callback_;
};
} // namespace detail

inline auto when_stopped(std::stop_token stop_token)
{
    return detail::stop_token_awaiter(stop_token);
}

} // namespace coral