#pragma once

#include <coral/concepts.hpp>
#include <coral/detail/task_awaiter.hpp>

#include <coroutine>
#include <exception>
#include <functional>
#include <type_traits>
#include <utility>
#include <variant>

namespace coral {

template <typename T = void>
class task;

namespace detail {

// Non-template base promise type containing continuation management
class task_promise_base {
    friend struct FinalAwaiter;

    struct final_awaiter {
        bool await_ready() noexcept { return false; }

        template <typename Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> my_coro) noexcept
        {
            return my_coro.promise().continuation_;
        }

        void await_resume() noexcept {}
    };

public:
    task_promise_base() noexcept = default;

    std::suspend_always initial_suspend() noexcept { return {}; }

    final_awaiter final_suspend() noexcept { return {}; }

    void set_continuation(std::coroutine_handle<> continuation) noexcept
    {
        continuation_ = continuation;
    }

protected:
    std::coroutine_handle<> continuation_{std::noop_coroutine()};
};

template <typename T>
class task_promise final : public task_promise_base {
    using value_t = std::conditional_t<std::is_reference_v<T>,
        std::reference_wrapper<std::remove_reference_t<T>>,
        T>;

public:
    task<T> get_return_object() noexcept;

    void unhandled_exception() noexcept { result_.template emplace<2>(std::current_exception()); }

    template <typename Value>
        requires std::constructible_from<value_t, Value&&>
    void return_value(Value&& value) noexcept(std::is_nothrow_constructible_v<T, Value&&>)
    {
        result_.template emplace<1>(std::forward<Value>(value));
    }

    T result()
    {
        if (std::holds_alternative<std::exception_ptr>(result_)) {
            std::rethrow_exception(std::get<std::exception_ptr>(result_));
        }
        return std::move(std::get<1>(result_));
    }

private:
    std::variant<std::monostate, value_t, std::exception_ptr> result_;
};

// Specialization for void - no return value
template <>
class task_promise<void> final : public task_promise_base {
public:
    task<void> get_return_object() noexcept;

    void unhandled_exception() noexcept { result_.template emplace<1>(std::current_exception()); }

    void return_void() noexcept {}

    void result()
    {
        if (std::holds_alternative<std::exception_ptr>(result_)) {
            std::rethrow_exception(std::get<std::exception_ptr>(result_));
        }
    }

private:
    std::variant<std::monostate, std::exception_ptr> result_;
};

} // namespace detail

template <typename T>
class [[nodiscard]] task {
public:
    using promise_type = detail::task_promise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    explicit task(handle_type handle) noexcept
        : coroutine_handle_(handle)
    {
    }

    task(task&& other) noexcept
        : coroutine_handle_(std::exchange(other.coroutine_handle_, nullptr))
    {
    }
    task& operator=(task&& other) noexcept
    {
        if (this != &other) {
            if (coroutine_handle_) {
                coroutine_handle_.destroy();
            }
            coroutine_handle_ = std::exchange(other.coroutine_handle_, nullptr);
        }
        return *this;
    }

    ~task()
    {
        if (coroutine_handle_) {
            coroutine_handle_.destroy();
        }
    }

    task(const task&) = delete;
    task& operator=(const task&) = delete;

    auto operator co_await() noexcept
    {
        return detail::task_awaiter{std::exchange(coroutine_handle_, nullptr), true};
    }

private:
    handle_type coroutine_handle_;
};

namespace detail {

template <typename T>
inline task<T> task_promise<T>::get_return_object() noexcept
{
    return task<T>{std::coroutine_handle<task_promise<T>>::from_promise(*this)};
}

inline task<void> task_promise<void>::get_return_object() noexcept
{
    return task<void>{std::coroutine_handle<task_promise<void>>::from_promise(*this)};
}

} // namespace detail

} // namespace coral
