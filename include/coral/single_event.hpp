#pragma once

#include <atomic>
#include <cassert>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace coral {

class single_event_error : public std::domain_error {
public:
    using std::domain_error::domain_error;
};

template <typename T = void>
class single_event final {
    enum state_t : std::uint8_t { has_sender = 0x1, has_value = 0x2, has_awaiter = 0x4 };

public:
    class sender final {
    public:
        explicit sender(single_event<T>* event) noexcept
            : event_(event)
        {
            attach();
        }

        sender(sender&& other) noexcept
            : event_(std::exchange(other.event_, nullptr))
        {
        }

        sender& operator=(sender&& other) noexcept
        {
            if (this != &other) {
                release();
                event_ = std::exchange(other.event_, nullptr);
            }
            return *this;
        }

        sender(const sender&) = delete;
        sender& operator=(const sender&) = delete;

        ~sender() { release(); }

        template <typename U>
            requires(!std::is_same_v<T, void>)
        void set_value(U&& value)
        {
            if (auto* const event = std::exchange(event_, nullptr)) {
                event->set_value(std::forward<U>(value));
            } else {
                throw single_event_error("can not set value");
            }
        }

        void set_value()
            requires(std::is_same_v<T, void>)
        {
            if (auto* const event = std::exchange(event_, nullptr)) {
                event->set_value();
            } else {
                throw single_event_error("can not set value");
            }
        }

        void set_exception(std::exception_ptr e)
        {
            if (auto* const event = std::exchange(event_, nullptr)) {
                event->set_exception(std::move(e));
            } else {
                throw single_event_error("can not set value");
            }
        }

    private:
        void attach()
        {
            if (event_) {
                event_->set_sender();
            }
        }
        void release()
        {
            if (event_) {
                event_->release_sender();
            }
        }

        single_event<T>* event_;
    };

    single_event() noexcept = default;

    single_event(const single_event&) = delete;
    single_event& operator=(const single_event&) = delete;

    single_event(single_event&&) = delete;
    single_event& operator=(single_event&&) = delete;

    ~single_event() = default;

    sender get_sender() noexcept { return sender{this}; }

    auto operator co_await() noexcept
    {
        struct awaiter {
            single_event* event;

            static bool is_ready(const std::uint8_t flag) noexcept
            {
                return ((flag & state_t::has_value) == state_t::has_value)
                    | ((flag & state_t::has_sender) != state_t::has_sender);
            }

            bool await_ready() const noexcept
            {
                const auto flag = event->state_.load(std::memory_order::acquire);
                return is_ready(flag);
            }

            bool await_suspend(std::coroutine_handle<> h) noexcept
            {
                event->waiting_ = h;
                const auto flag =
                    event->state_.fetch_or(state_t::has_awaiter, std::memory_order::acq_rel);
                return !is_ready(flag);
            }

            T await_resume()
            {
                const auto flag = event->state_.load(std::memory_order::relaxed);
                if ((flag & state_t::has_sender) != state_t::has_sender) {
                    throw single_event_error("no sender");
                }
                if (std::holds_alternative<std::exception_ptr>(event->value_)) {
                    std::rethrow_exception(std::get<std::exception_ptr>(event->value_));
                }
                if constexpr (!std::is_same_v<T, void>) {
                    return std::move(std::get<2>(event->value_));
                }
            }
        };
        return awaiter{this};
    }

    auto get_awaitable() noexcept { return this->operator co_await(); }

private:
    std::atomic<std::uint8_t> state_{0};
    using value_t = std::conditional_t<std::is_same_v<T, void>,
        std::variant<std::monostate, std::exception_ptr>,
        std::variant<std::monostate, std::exception_ptr, T>>;
    value_t value_;
    std::coroutine_handle<> waiting_{};

    void set_sender()
    {
        const auto flag = state_.fetch_or(state_t::has_sender, std::memory_order::acq_rel);
        if (flag & state_t::has_sender) {
            throw single_event_error("sender already exist");
        }
    }

    void release_sender()
    {
        const auto flag = state_.fetch_and(
            static_cast<std::uint8_t>(~state_t::has_sender), std::memory_order::acq_rel);
        if (flag & state_t::has_value) {
            return;
        }
        if (flag & state_t::has_awaiter) {
            set_exception(std::make_exception_ptr(single_event_error("sender deleted")));
        }
    }

    template <typename U>
        requires(!std::is_same_v<T, void> && std::constructible_from<T, U &&>)
    void set_value(U&& value)
    {
        value_.template emplace<2>(std::forward<U>(value));
        on_result_set();
    }

    void set_value()
        requires(std::is_same_v<T, void>)
    {
        on_result_set();
    }

    void set_exception(std::exception_ptr e)
    {
        value_.template emplace<1>(std::move(e));
        on_result_set();
    }

    void on_result_set()
    {
        const auto flag = state_.fetch_or(state_t::has_value, std::memory_order::acq_rel);
        if (flag & state_t::has_awaiter) {
            waiting_.resume();
        }
    }
};

} // namespace coral
