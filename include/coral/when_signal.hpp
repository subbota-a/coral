#pragma once
#include <coral/concepts.hpp>

#include <atomic>
#include <coroutine>
#include <csignal>
#include <stdexcept>
#include <stop_token>
#include <thread>

namespace coral {
namespace detail {

class interrupt_state {
    inline static std::atomic<std::atomic<bool>*> flag{nullptr};

    static void sig_handler(int)
    {
        auto* flag_ptr = flag.load(std::memory_order::acquire);
        if (flag_ptr) {
            flag_ptr->store(true);
            flag_ptr->notify_one();
        }
    }

public:
    static void run(int sig, std::stop_token stop_token, std::coroutine_handle<> handle)
    {
        std::atomic<bool> my_flag{false};
        std::atomic<bool>* null_ptr{nullptr};
        if (!flag.compare_exchange_strong(null_ptr, &my_flag)) {
            handle();
            return;
        }
        {
            std::stop_callback stop{stop_token, [&my_flag] {
                                        my_flag.store(true);
                                        my_flag.notify_one();
                                    }};
            std::signal(sig, &interrupt_state::sig_handler);

            my_flag.wait(false);
        }
        std::signal(sig, SIG_DFL);
        flag.store(nullptr);
        handle();
    }
};

class interrupt_awaiter {
    std::stop_token stop_token_;
    int sig_;
    inline static std::atomic<bool> reinterant_flag{false};

public:
    interrupt_awaiter(std::stop_token stop_token, int sig)
        : stop_token_(stop_token)
        , sig_(sig)
    {
    }

    bool await_ready() const noexcept { return stop_token_.stop_requested(); }

    void await_suspend(std::coroutine_handle<> continuation)
    {
        if (reinterant_flag.exchange(true, std::memory_order::relaxed)) {
            throw std::runtime_error("signal handler already set");
        }
        std::jthread(&interrupt_state::run, sig_, stop_token_, continuation).detach();
    }

    void await_resume() const noexcept { reinterant_flag.store(false, std::memory_order::relaxed); }
};

struct interrupt_awaitable {
    std::stop_token stop_token;
    int sig;
    interrupt_awaiter operator co_await() const noexcept { return {stop_token, sig}; }
};

} // namespace detail

inline awaitable auto when_signal(std::stop_token stop_token, int sig)
{
    return detail::interrupt_awaitable{stop_token, sig};
}

} // namespace coral