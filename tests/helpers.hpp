#pragma once
#include <coral/detail/config.hpp>
#include <coral/task.hpp>

#include <chrono>
#include <coroutine>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#if CORAL_EXPECTED
#include <expected>
#endif

// ============================================================================
// Common Test Helpers
// ============================================================================
template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

// Simple tasks returning different types
coral::task<int> make_int_task(int value) { co_return value; }

coral::task<void> make_void_task() { co_return; }

coral::task<std::string> make_string_task(std::string value) { co_return value; }

coral::task<std::unique_ptr<int>> make_unique_ptr_task(int value)
{
    co_return std::make_unique<int>(value);
}

// Global variables for reference tests
int test_int_value = 142;
const int test_const_int_value = 100;

// Tasks returning references
coral::task<int&> make_int_ref_task() { co_return test_int_value; }

coral::task<const int&> make_const_int_ref_task() { co_return test_const_int_value; }

// Tasks returning pointers
coral::task<int*> make_int_ptr_task() { co_return &test_int_value; }

coral::task<const int*> make_const_int_ptr_task() { co_return &test_const_int_value; }

coral::task<int> make_throwing_int_task(const char* message)
{
    throw std::runtime_error(message);
    co_return 0;
}

coral::task<void> make_throwing_void_task(const char* message)
{
    throw std::runtime_error(message);
    co_return;
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

coral::task<int>
make_stoppable_task(std::stop_token stop_token, int value, std::chrono::milliseconds delay)
{
    for (int i = 0; i < 10; ++i) {
        if (stop_token.stop_requested()) {
            throw std::runtime_error("stopped");
        }
        co_await async_delay(delay / 10);
    }
    co_return value;
}

coral::task<int> make_delayed_int_task(int value, std::chrono::milliseconds delay)
{
    co_await async_delay(delay);
    co_return value;
}

coral::task<void> make_delayed_void_task(std::chrono::milliseconds delay)
{
    co_await async_delay(delay);
    co_return;
}

coral::task<void> make_delayed_throwing_void_task(const char* message, std::chrono::milliseconds delay)
{
    co_await async_delay(delay);
    throw std::runtime_error(message);
    co_return;
}

coral::task<int> make_delayed_throwing_int_task(const char* message, std::chrono::milliseconds delay)
{
    co_await async_delay(delay);
    throw std::runtime_error(message);
    co_return 10;
}

#if CORAL_EXPECTED
using int2_error_t = std::expected<int, std::error_code>;

auto make_int2error_code(std::variant<int, std::error_code> v,
    std::chrono::milliseconds delay = std::chrono::milliseconds{0}) -> coral::task<int2_error_t>
{
    if (delay != std::chrono::milliseconds{0}) {
        co_await async_delay(delay);
    }
    co_return std::visit(
        overloaded{
            [](int value) -> int2_error_t { return value; },
            [](std::error_code error) -> int2_error_t { return std::unexpected(error); },
        },
        v);
}

using double2error_t = std::expected<double, std::error_code>;

auto make_double2error_code(std::variant<double, std::error_code> v,
    std::chrono::milliseconds delay = std::chrono::milliseconds{0}) -> coral::task<double2error_t>
{
    if (delay != std::chrono::milliseconds{0}) {
        co_await async_delay(delay);
    }
    co_return std::visit(
        overloaded{
            [](double value) -> double2error_t { return value; },
            [](std::error_code error) -> double2error_t { return std::unexpected(error); },
        },
        v);
}

using int2unique_ptr_t = std::expected<int, std::unique_ptr<int>>;

auto make_int2unique_ptr(std::variant<int, std::unique_ptr<int>> v,
    std::chrono::milliseconds delay = std::chrono::milliseconds{0}) -> coral::task<int2unique_ptr_t>
{
    if (delay != std::chrono::milliseconds{0}) {
        co_await async_delay(delay);
    }
    co_return std::visit(overloaded{
                             [](int value) -> int2unique_ptr_t { return value; },
                             [](std::unique_ptr<int> error) -> int2unique_ptr_t {
                                 return std::unexpected(std::move(error));
                             },
                         },
        std::move(v));
}

using unique_ptr2int_t = std::expected<std::unique_ptr<int>, int>;

auto make_unique_ptr2int(std::variant<std::unique_ptr<int>, int> v,
    std::chrono::milliseconds delay = std::chrono::milliseconds{0}) -> coral::task<unique_ptr2int_t>
{
    if (delay != std::chrono::milliseconds{0}) {
        co_await async_delay(delay);
    }
    co_return std::visit(
        overloaded{
            [](std::unique_ptr<int> value) -> unique_ptr2int_t { return std::move(value); },
            [](int error) -> unique_ptr2int_t { return std::unexpected(error); },
        },
        std::move(v));
}

#endif
