#pragma once

#include <coral/async_result.hpp>
#include <coral/detail/config.hpp>
#include <coral/detail/traits.hpp>
#include <coral/traits.hpp>

#include <coroutine>
#include <exception>
#include <functional>
#include <ranges>
#include <type_traits>
#include <utility>
#include <variant>

namespace coral::detail {

using ready_signature = std::coroutine_handle<>(bool success);
using ready_callback =
    std::function<ready_signature>; // TODO: make allocation free light implementation

struct adapter_promise_final_awaiter {
    ready_callback callback;
    bool success{false};

    bool await_ready() noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept
    {
        return callback ? callback(success) : std::noop_coroutine();
    }

    void await_resume() noexcept {}
};

template <typename T>
class adapter_promise {
public:
    using handle_t = std::coroutine_handle<adapter_promise<T>>;

    adapter_promise() noexcept = default;

    auto get_return_object() noexcept { return handle_t::from_promise(*this); }

    void set_ready_callback(ready_callback callback) noexcept(
        std::is_nothrow_copy_constructible_v<ready_callback>)
    {
        callback_ = std::move(callback);
    }

    std::suspend_always initial_suspend() noexcept { return {}; }
    auto final_suspend() noexcept { return get_final_awaiter(); }

    void unhandled_exception() noexcept { result_.template emplace<2>(std::current_exception()); }

    void return_value(T&& value) noexcept
    {
        result_.template emplace<1>(static_cast<T&&>(value));
    }

    T get_result_value()
    {
        if (std::holds_alternative<std::exception_ptr>(result_)) {
            std::rethrow_exception(std::get<std::exception_ptr>(result_));
        }
        return static_cast<T&&>(std::get<1>(result_));
    }

    async_result<T> get_result()
    {
        if (std::holds_alternative<std::exception_ptr>(result_)) {
            return async_result<T>{std::get<std::exception_ptr>(result_)};
        }
        return async_result<T>{static_cast<T&&>(std::get<1>(result_))};
    }

private:
    using value_t = std::conditional_t<std::is_reference_v<T>,
        std::reference_wrapper<std::remove_reference_t<T>>,
        T>;
    std::variant<std::monostate, value_t, std::exception_ptr> result_;
    ready_callback callback_;

    adapter_promise_final_awaiter get_final_awaiter() const noexcept
    {
#if CORAL_EXPECTED
        if constexpr (is_expected_v<T>) {
            const auto success = result_.index() == 1 && std::get<1>(result_).has_value();
            return {.callback = std::move(callback_), .success = success};
        } else {
            return {.callback = std::move(callback_), .success = result_.index() == 1};
        }
#else
        return {.callback = std::move(callback_), .success = result_.index() == 1};
#endif
    }
};

// Specialization for void
template <>
class adapter_promise<void> {
public:
    using handle_t = std::coroutine_handle<adapter_promise<void>>;

    adapter_promise() noexcept = default;

    auto get_return_object() noexcept { return handle_t::from_promise(*this); }

    void set_ready_callback(ready_callback callback) noexcept(
        std::is_nothrow_copy_constructible_v<ready_callback>)
    {
        callback_ = std::move(callback);
    }

    std::suspend_always initial_suspend() noexcept { return {}; }
    auto final_suspend() noexcept { return get_final_awaiter(); }

    void unhandled_exception() noexcept { exception_ = std::current_exception(); }

    void return_void() noexcept {}

    void get_result_value()
    {
        if (exception_) {
            std::rethrow_exception(exception_);
        }
    }

    async_result<void> get_result()
    {
        if (exception_) {
            return async_result<void>{exception_};
        }
        return async_result<void>{};
    }

private:
    std::exception_ptr exception_;
    ready_callback callback_;

    adapter_promise_final_awaiter get_final_awaiter() const noexcept
    {
        return {.callback = std::move(callback_), .success = !exception_};
    }
};

// Sync wait task wrapper
template <typename T>
class adaptor_task {
public:
    using promise_type = adapter_promise<T>;
    using handle_type = std::coroutine_handle<promise_type>;
    using result_type = T;

    adaptor_task(handle_type handle) noexcept
        : handle_(handle)
    {
    }

    adaptor_task(adaptor_task&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr))
    {
    }

    ~adaptor_task()
    {
        if (handle_) {
            handle_.destroy();
        }
    }

    adaptor_task(const adaptor_task&) = delete;
    adaptor_task& operator=(const adaptor_task&) = delete;

    void start(ready_callback callback) noexcept
    {
        handle_.promise().set_ready_callback(std::move(callback));
        handle_.resume();
    }

    handle_type setup(ready_callback callback) noexcept
    {
        handle_.promise().set_ready_callback(std::move(callback));
        return handle_;
    }

    decltype(auto) get_result_value() { return handle_.promise().get_result_value(); }

    decltype(auto) get_result() { return handle_.promise().get_result(); }

private:
    handle_type handle_;
};

// Factory function for non-void results - uses co_yield
template <awaitable Awaitable>
adaptor_task<result_of<Awaitable>> make_adapter_task(Awaitable awaitable)
{
    if constexpr (std::is_void_v<result_of<Awaitable>>) {
        co_await std::move(awaitable);
    } else {
        co_return co_await std::move(awaitable);
    }
}

template <awaitable... Awaitable>
auto make_adapter_tasks(Awaitable&&... awaitables)
{
    return std::make_tuple(detail::make_adapter_task(std::move(awaitables))...);
}

template <typename Range>
    requires std::ranges::range<Range> && awaitable<std::ranges::range_value_t<Range>>
auto make_adapter_tasks(Range&& awaitables)
{
    using type = detail::adaptor_task<result_of<std::ranges::range_value_t<Range>>>;

    std::vector<type> adapter_tasks;

    if constexpr (std::ranges::sized_range<Range>) {
        adapter_tasks.reserve(std::size(awaitables));
    }

    for (auto&& item : awaitables) {
        adapter_tasks.push_back(detail::make_adapter_task(std::move(item)));
    }

    return adapter_tasks;
}

} // namespace coral::detail