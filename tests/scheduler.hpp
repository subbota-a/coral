#include <condition_variable>
#include <coroutine>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

class scheduler;

class static_thread_pool final {
public:
    static_thread_pool(size_t size);
    ~static_thread_pool();

    void stop_request();

    void join();

    void enqueue(std::function<void()> h);

    ::scheduler scheduler() noexcept;

private:
    struct thread_context {
        std::mutex mutex;
        std::condition_variable_any cv;
        std::deque<std::function<void()>> queue;
        std::jthread thread;
    };
    const size_t size_mask;
    std::vector<std::unique_ptr<thread_context>> threads_;
    std::atomic<size_t> next_thread_{0};

    void thread_func(std::stop_token stop_token, size_t index);
};

class scheduler final {
public:
    explicit scheduler(static_thread_pool& pool)
        : pool_(&pool)
    {
    }

    auto operator co_await() const noexcept
    {
        struct awaiter {
            static_thread_pool* pool;
            constexpr bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> h) { pool->enqueue(h); }

            constexpr void await_resume() noexcept {}
        };
        return awaiter{pool_};
    }

    void schedule(std::coroutine_handle<> h) const noexcept { pool_->enqueue(h); }

private:
    static_thread_pool* pool_;
};