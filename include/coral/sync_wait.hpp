#pragma once

#include <condition_variable>
#include <coral/concepts.hpp>
#include <coral/detail/adapter_task.hpp>
#include <coral/task.hpp>
#include <coral/traits.hpp>
#include <coroutine>
#include <mutex>

namespace coral {

namespace detail {

// Event for synchronization
class sync_event {
public:
    void set() noexcept {
        std::unique_lock lock{mutex_};
        signaled_ = true;
        lock.unlock();
        cv_.notify_one();
    }

    void wait() noexcept {
        std::unique_lock lock{mutex_};
        cv_.wait(lock, [this] { return signaled_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool signaled_{false};
};

} // namespace detail

/// Synchronously wait for an awaitable to complete
template <awaitable Awaitable>
decltype(auto) sync_wait(Awaitable&& awaitable) {
    detail::sync_event event;
    auto task = detail::make_adapter_task(std::forward<Awaitable>(awaitable));
    task.start([&event] (bool){
        event.set();
        return std::noop_coroutine();
    });
    event.wait();
    return task.get_result_value();
}

} // namespace coral
