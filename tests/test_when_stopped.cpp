#include <coral/sync_wait.hpp>
#include <coral/when_stopped.hpp>

#include <catch2/catch_test_macros.hpp>

#include <stop_token>
#include <thread>
using namespace std::chrono_literals;

TEST_CASE("when_stopped")
{
    std::stop_source stop_source;
    auto task = [](std::stop_token stop_token) -> coral::task<> {
        co_await coral::when_stopped(stop_token);
    };

    SECTION("stop requested before when_stopped")
    {
        stop_source.request_stop();

        coral::sync_wait(task(stop_source.get_token()));
        SUCCEED("");
    }
    SECTION("stop requested async")
    {
        auto thread = std::jthread([&stop_source] {
            std::this_thread::sleep_for(100ms);
            stop_source.request_stop();
        });
        coral::sync_wait(task(stop_source.get_token()));
        SUCCEED("");
    }
}
