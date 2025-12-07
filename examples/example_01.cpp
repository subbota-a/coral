#include <coral/task.hpp>

#include "coral/sync_wait.hpp"
#include "coral/when_any.hpp"
#include "coral/when_signal.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stop_token>
#include <thread>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

coral::task<> enumerate_directories(std::filesystem::path directory, std::stop_token token)
{
    for (const fs::directory_entry& dir_entry : fs::recursive_directory_iterator(
             directory, fs::directory_options::skip_permission_denied)) {

        std::cout << dir_entry << '\n';

        if (token.stop_requested()) {
            co_return;
        }
    }
}

struct delay_awaitable {
    std::chrono::milliseconds delay;

    explicit delay_awaitable(std::chrono::milliseconds d)
        : delay(d)
    {
    }

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> continuation)
    {
        std::thread([continuation, delay = this->delay]() {
            std::this_thread::sleep_for(delay);
            continuation.resume();
        }).detach();
    }

    void await_resume() noexcept {}
};

delay_awaitable async_delay(std::chrono::milliseconds delay) { return delay_awaitable{delay}; }

coral::task<> stoppable_delay(std::stop_token stop_token, std::chrono::milliseconds delay)
{
    for (int i = 0; i < 10; ++i) {
        if (stop_token.stop_requested()) {
            break;
        }
        co_await async_delay(delay / 10);
    }
    co_return;
}

int main()
{
    std::cout << "=== Coral Task Examples ===\n";
    std::cout << "It will iterate all directories until 60s or user presses ctrl+c\n";
    std::cout << "Press Enter to start\n";
    std::cin.get();

    std::stop_source stop_source;

    auto result = coral::sync_wait(coral::when_any(stop_source,
        coral::when_signal(stop_source.get_token(), SIGINT),
        stoppable_delay(stop_source.get_token(), 10s),
        enumerate_directories("/", stop_source.get_token())));

    if (result.first == 0)        {
        std::cout << "A user stopped the execution\n";
    }else if (result.first == 1){
        std::cout << "The timeout stopped the execution\n";
    }

    std::cout << "\n=== All Examples Completed ===\n";
    return 0;
}
