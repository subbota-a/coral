#pragma once
#include <coral/concepts.hpp>

#include <atomic>
#include <coroutine>
#include <cstdlib>
#include <utility>

namespace coral {

class mutex {
public:
    explicit mutex() noexcept = default;

    mutex(const mutex&) = delete;
    mutex& operator=(const mutex&) = delete;

    mutex(mutex&&) = delete;
    mutex& operator=(mutex&&) = delete;

    struct awaiter_node {
        awaiter_node* next{nullptr};
        std::coroutine_handle<> handle{};
    };

    inline static constexpr awaiter_node* unlocked = nullptr;
    inline static awaiter_node* const locked = reinterpret_cast<awaiter_node*>(1);

    /// When mutex is unlocked the `next` field becomes `mutex::unlocked`
    /// and the mutex becomes locked and the head of the awaiting list is `mutex::locked`.
    /// The second coroutine trying to lock the mutex gets `mutex::locked` in
    /// the `next` field and mutex saves `cur` as a head of the awaiting list.
    /// The third coroutine gets the second coroutine in `next` field
    /// and it becomes the new head of the awaiting list
    awaiter_node* try_lock(awaiter_node* cur) noexcept
    {
        auto cur_next_copy = cur->next = list_.load(std::memory_order::relaxed);
        auto* next = cur->next == mutex::unlocked ? mutex::locked : cur;
        while (!list_.compare_exchange_weak(
            cur->next, next, std::memory_order::acq_rel, std::memory_order_relaxed)) {
            cur_next_copy = cur->next;
            next = cur->next == mutex::unlocked ? mutex::locked : cur;
        }
        return cur_next_copy;
    }

    /// Coroutine which `next` field is one of the terminate state (`mutex::locked` or
    /// `mutex::unlocked`) has to call `try_unlock()`. If mutex doesn't have any more awaiting
    /// coroutines it unlocks and return `mutex::locked` If mutex has awaiting coroutines it returns
    /// the head of them and save `mutex::locked` as a new head of awaiting queue. If mutex is
    /// unlocked it terminates the application.
    awaiter_node* try_unlock() noexcept
    {
        auto* last = list_.load(std::memory_order::relaxed);
        auto* next = (last == locked) ? unlocked : locked;
        while (!list_.compare_exchange_weak(
            last, next, std::memory_order::acq_rel, std::memory_order::relaxed)) {
            next = (last == locked) ? unlocked : locked;
        }
        if (last == unlocked) [[unlikely]] {
            std::abort();
        }
        return last;
    }

private:
    std::atomic<awaiter_node*> list_{unlocked};
};

struct sync_scheduler {
    void schedule(std::coroutine_handle<> h) const { h(); }
};

template <scheduler Scheduler = sync_scheduler>
class [[nodiscard]] unique_lock {
private:
    mutex* mutex_;
    mutex::awaiter_node* next_;
    Scheduler scheduler_;

public:
    class awaiter : public mutex::awaiter_node {
    public:
        explicit awaiter(mutex& m, Scheduler scheduler)
            : mutex_{&m}
            , scheduler_(std::move(scheduler))
        {
        }
        constexpr bool await_ready() const noexcept { return false; }

        bool await_suspend(std::coroutine_handle<> h) noexcept
        {
            this->handle = h;
            auto next_copy = mutex_->try_lock(this);
            // we can be deleted after this point, so I use next_copy

            // not suspend if mutex was unlocked
            return !(next_copy == mutex::unlocked);
        }

        unique_lock await_resume() { return unique_lock{*mutex_, next, std::move(scheduler_)}; }

    private:
        mutex* mutex_{nullptr};
        Scheduler scheduler_;
    };

    explicit unique_lock(mutex& mutex, mutex::awaiter_node* next, Scheduler scheduler)
        : mutex_(&mutex)
        , next_(next)
        , scheduler_(std::move(scheduler))
    {
    }
    ~unique_lock() { unlock(); }

    unique_lock(const unique_lock&) = delete;
    unique_lock& operator=(const unique_lock&) = delete;

    unique_lock(unique_lock&& other) noexcept
        : mutex_(std::exchange(other.mutex_, nullptr))
        , next_(std::exchange(other.next_, mutex::unlocked))
    {
    }
    unique_lock& operator=(unique_lock&& other)
    {
        if (&other != this) {
            unlock();
            mutex_ = std::exchange(other.mutex_, nullptr);
            next_ = std::exchange(other.next_, mutex::unlocked);
        }
        return *this;
    }

    void unlock()
    {
        if ((next_ != mutex::unlocked) & (next_ != mutex::locked)) {
            scheduler_.schedule(next_->handle);
        } else if (mutex_) {
            auto* next = mutex_->try_unlock();
            if (next != mutex::locked) {
                scheduler_.schedule(next->handle);
            }
        }
        next_ = mutex::locked;
        mutex_ = nullptr;
    }
};

inline auto when_locked(mutex& mutex) { return unique_lock<>::awaiter{mutex, sync_scheduler{}}; }

template <scheduler Scheduler>
auto when_locked(mutex& mutex, Scheduler scheduler)
{
    return typename unique_lock<Scheduler>::awaiter{mutex, std::move(scheduler)};
}
} // namespace coral