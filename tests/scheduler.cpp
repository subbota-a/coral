#include "scheduler.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>

static_thread_pool::static_thread_pool(size_t size)
    : size_mask(size - 1)
{
    threads_.reserve(size);
    for (size_t i = 0; i < size; ++i) {
        threads_.push_back(std::make_unique<thread_context>());
    }
    for (size_t i = 0; i < size; ++i) {
        threads_[i]->thread =
            std::jthread(std::bind_front(&static_thread_pool::thread_func, this), i);
    }
}
static_thread_pool::~static_thread_pool()
{
    stop_request();
    join();
}

void static_thread_pool::stop_request()
{
    for (auto& t : threads_) {
        t->thread.get_stop_source().request_stop();
    }
}

void static_thread_pool::join()
{
    for (auto& t : threads_) {
        if (t->thread.joinable()) {
            t->thread.join();
        }
    }
}

void static_thread_pool::enqueue(std::function<void()> h)
{
    const auto i = next_thread_.fetch_add(1, std::memory_order::relaxed) & size_mask;
    const auto& t = threads_[i];
    {
        const auto lock = std::scoped_lock(t->mutex);
        t->queue.push_back(std::move(h));
    }
    t->cv.notify_one();
}

scheduler static_thread_pool::scheduler() noexcept { return ::scheduler{*this}; }

void static_thread_pool::thread_func(std::stop_token stop_token, size_t index)
{
    const auto& t = threads_[index];
    auto lock = std::unique_lock(t->mutex);
    while (t->cv.wait(lock, stop_token, [context = t.get()] { return !context->queue.empty(); })) {
        auto h = t->queue.front();
        t->queue.pop_front();
        lock.unlock();
        h();
        lock.lock();
    }
}
