#pragma once

#include <coroutine>

namespace coral::detail {

template <typename Promise>
class task_awaiter {
public:
    using handle_type = std::coroutine_handle<Promise>;

    explicit task_awaiter(handle_type handle, bool owning)
        : promise_handle_(handle)
        , owning_(owning)
    {
    }

    ~task_awaiter()
    {
        if (owning_ && promise_handle_) {
            promise_handle_.destroy();
        }
    }

    task_awaiter(const task_awaiter&) = delete;
    task_awaiter(task_awaiter&&) = delete;

    task_awaiter& operator=(const task_awaiter&) = delete;
    task_awaiter& operator=(task_awaiter&&) = delete;

    bool await_ready() const noexcept { return !promise_handle_ || promise_handle_.done(); }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept
    {
        promise_handle_.promise().set_continuation(continuation);
        return promise_handle_;
    }

    decltype(auto) await_resume() { return promise_handle_.promise().result(); }

private:
    handle_type promise_handle_;
    bool owning_;
};

} // namespace coral::detail