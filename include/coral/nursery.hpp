#pragma once

#include <coral/concepts.hpp>
#include <coral/detail/task_awaiter.hpp>

#include <atomic>
#include <coroutine>
#include <exception>
#include <functional>
#include <type_traits>
#include <utility>
#include <variant>

namespace coral {

template <typename T = void>
class nursery_task;

namespace detail {
class nursery_task_promise_base;
}

class nursery {
public:
    explicit nursery(detail::nursery_task_promise_base& promise)
        : promise_(promise)
    {
    }

    template <awaitable Awaitable>
        requires(std::is_rvalue_reference_v<Awaitable &&>)
    void start(Awaitable&& awaitable);

private:
    std::reference_wrapper<detail::nursery_task_promise_base> promise_;
};

namespace detail {

struct spawn_task;

class spawn_promise {
public:
    using handle_t = std::coroutine_handle<spawn_promise>;

    spawn_promise() noexcept = default;

    spawn_task get_return_object() noexcept;

    std::suspend_always initial_suspend() const noexcept { return {}; }
    auto final_suspend() noexcept
    {
        finish().resume();
        return std::suspend_never{};
    }

    void unhandled_exception() const noexcept {}
    void return_void() const noexcept {}

    void start(nursery_task_promise_base& nursery_promise) noexcept;
    std::coroutine_handle<> finish() noexcept;

private:
    nursery_task_promise_base* nursery_promise_{nullptr};
};

struct spawn_task {
    using promise_type = spawn_promise;
    using handle_t = spawn_promise::handle_t;
    handle_t handle;

    void start(nursery_task_promise_base& nursery_promise) noexcept
    {
        handle.promise().start(nursery_promise);
    }
};

inline spawn_task spawn_promise::get_return_object() noexcept { return {handle_t::from_promise(*this)}; }

template <awaitable Awaitable>
spawn_task spawn_adapter(Awaitable awaitable)
{
    (void)co_await std::move(awaitable);
    co_return;
}

// Non-template base promise type containing continuation management
class nursery_task_promise_base {

    struct final_awaiter {
        constexpr bool await_ready() const noexcept { return false; }

        template <typename Promise>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> my_coro) noexcept
        {
            return my_coro.promise().child_completed();
        }

        void await_resume() noexcept {}
    };

public:
    nursery_task_promise_base() noexcept = default;

    std::suspend_always initial_suspend() noexcept { return {}; }

    final_awaiter final_suspend() noexcept { return {}; }

    void set_continuation(std::coroutine_handle<> continuation) noexcept
    {
        continuation_ = continuation;
    }

    std::coroutine_handle<> child_completed() noexcept
    {
        // wait for all children AND nursery coroutine is done
        if (child_counter_.fetch_sub(1, std::memory_order::acq_rel) == 0) {
            return continuation_;
        }
        return std::noop_coroutine();
    }

    void child_started() noexcept { child_counter_.fetch_add(1, std::memory_order::acq_rel); }

protected:
    std::atomic<int> child_counter_{0};
    std::coroutine_handle<> continuation_{std::noop_coroutine()};
};

inline void spawn_promise::start(nursery_task_promise_base& nursery_promise) noexcept
{
    nursery_promise_ = &nursery_promise;
    nursery_promise_->child_started();
    handle_t::from_promise(*this).resume();
}

inline std::coroutine_handle<> spawn_promise::finish() noexcept
{
    return nursery_promise_->child_completed();
}

template <typename T>
class nursery_task_promise final : public nursery_task_promise_base {
    using value_t = std::conditional_t<std::is_reference_v<T>,
        std::reference_wrapper<std::remove_reference_t<T>>,
        T>;

public:
    nursery_task<T> get_return_object() noexcept;

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
class nursery_task_promise<void> final : public nursery_task_promise_base {
public:
    nursery_task<void> get_return_object() noexcept;

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

class nursery_awaiter final {
public:
    bool await_ready() noexcept { return false; }

    template <typename T>
    bool await_suspend(std::coroutine_handle<nursery_task_promise<T>> handle) noexcept
    {
        promise_ = &handle.promise();
        return false;
    }

    nursery await_resume() noexcept { return nursery{*promise_}; }

private:
    nursery_task_promise_base* promise_{nullptr};
};

} // namespace detail

template <typename T>
class [[nodiscard]] nursery_task {
public:
    using promise_type = detail::nursery_task_promise<T>;
    using handle_type = std::coroutine_handle<promise_type>;

    explicit nursery_task(handle_type handle) noexcept
        : coroutine_handle_(handle)
    {
    }

    nursery_task(nursery_task&& other) noexcept
        : coroutine_handle_(std::exchange(other.coroutine_handle_, nullptr))
    {
    }
    nursery_task& operator=(nursery_task&& other) noexcept
    {
        if (this != &other) {
            if (coroutine_handle_) {
                coroutine_handle_.destroy();
            }
            coroutine_handle_ = std::exchange(other.coroutine_handle_, nullptr);
        }
        return *this;
    }

    ~nursery_task()
    {
        if (coroutine_handle_) {
            coroutine_handle_.destroy();
        }
    }

    nursery_task(const nursery_task&) = delete;
    nursery_task& operator=(const nursery_task&) = delete;

    auto operator co_await() noexcept
    {
        return detail::task_awaiter{std::exchange(coroutine_handle_, nullptr), true};
    }

private:
    handle_type coroutine_handle_;
};

inline detail::nursery_awaiter get_nursery() { return {}; }

template <awaitable Awaitable>
    requires(std::is_rvalue_reference_v<Awaitable &&>)
void nursery::start(Awaitable&& awaitable)
{
    auto t = detail::spawn_adapter(std::move(awaitable));
    t.start(promise_.get());
}

namespace detail {

template <typename T>
inline nursery_task<T> nursery_task_promise<T>::get_return_object() noexcept
{
    return nursery_task<T>{std::coroutine_handle<nursery_task_promise<T>>::from_promise(*this)};
}

inline nursery_task<void> nursery_task_promise<void>::get_return_object() noexcept
{
    return nursery_task<void>{
        std::coroutine_handle<nursery_task_promise<void>>::from_promise(*this)};
}

} // namespace detail

} // namespace coral
