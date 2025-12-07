#include <coral/sync_wait.hpp>
#include <coral/when_all.hpp>
#include <coral/when_any.hpp>
#include <coral/when_signal.hpp>

#include "helpers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace std::chrono_literals;

TEST_CASE("when_signal", "[when_signal]")
{
    std::stop_source stop_source;

    SECTION("interrupt cancelled")
    {
        coral::sync_wait(coral::when_any(
            stop_source, async_delay(10ms), coral::when_signal(stop_source.get_token(), SIGINT)));

        SUCCEED();
    }
    SECTION("interrupt occur")
    {
        auto delay_raise = [](const std::chrono::milliseconds delay) -> coral::task<> {
            co_await async_delay(delay);
            std::raise(SIGINT);
        };

        coral::sync_wait(coral::when_any(
            stop_source, delay_raise(10ms), coral::when_signal(stop_source.get_token(), SIGINT)));

        SUCCEED();
    }
}

TEST_CASE("when_signal manual", "[when_signal][manual][.]")
{
    std::stop_source stop_source;
    std::cout << "Press Ctrl+C\n";
    coral::sync_wait(coral::when_signal(stop_source.get_token(), SIGINT));
    std::cout << "\nCtrl+C is handled\n";
    SUCCEED();
}

TEST_CASE("when_signal throws if it is called twice", "[when_signal]")
{
    std::stop_source stop_source;
    REQUIRE_THROWS_AS(coral::sync_wait(coral::when_all(stop_source,
                          coral::when_signal(stop_source.get_token(), SIGINT),
                          coral::when_signal(stop_source.get_token(), SIGINT))),
        std::runtime_error);
    SUCCEED();
}