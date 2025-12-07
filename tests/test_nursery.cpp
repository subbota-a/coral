#include <coral/nursery.hpp>
#include <coral/sync_wait.hpp>
#include <coral/task.hpp>

#include "helpers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace std::chrono_literals;

TEST_CASE("nursery_task with no children", "[nursery]")
{
    auto f = []() -> coral::nursery_task<> { co_return; };
    coral::sync_wait(f());
}

TEST_CASE("child task runs")
{
    int i = 0;
    auto t = [&i]() -> coral::task<> {
        co_await async_delay(1ms);
        i = 1;
    };
    auto n = [&]() -> coral::nursery_task<> {
        auto nursery = co_await coral::get_nursery();
        nursery.start(t());
    };
    coral::sync_wait(n());
    REQUIRE(i==1);
}

TEST_CASE("nursery finishes while child is running", "[nursery]")
{
    const auto start = std::chrono::steady_clock::now();

    auto f = []() -> coral::nursery_task<> {
        const auto start2 = std::chrono::steady_clock::now();
        auto nursery = co_await coral::get_nursery();
        nursery.start(async_delay(15ms));
        const auto interval2 = std::chrono::steady_clock::now() - start2;
        REQUIRE(interval2 < 5ms);
    };
    coral::sync_wait(f());
    const auto interval = std::chrono::steady_clock::now() - start;
    REQUIRE(interval >= 15ms);
}

TEST_CASE("nursery child finishes first", "[nursery]")
{
    auto f = []() -> coral::nursery_task<> {
        auto nursery = co_await coral::get_nursery();
        nursery.start(make_int_task(10));
        co_return;
    };
    coral::sync_wait(f());
}

TEST_CASE("nursery return value", "[nursery]")
{
    SECTION("int")
    {
        auto f = []() -> coral::nursery_task<int> { co_return 100; };
        REQUIRE(coral::sync_wait(f()) == 100);
    }
    SECTION("int&")
    {
        int g = 100;
        auto f = [&]() -> coral::nursery_task<int&> { co_return g; };
        REQUIRE(&coral::sync_wait(f()) == &g);
    }
    SECTION("const int&")
    {
        const int g = 100;
        auto f = [&]() -> coral::nursery_task<const int&> { co_return g; };
        REQUIRE(&coral::sync_wait(f()) == &g);
    }
    SECTION("unique_ptr<int>")
    {
        auto f = [&]() -> coral::nursery_task<std::unique_ptr<int>> {
            co_return std::make_unique<int>(100);
        };
        auto res = coral::sync_wait(f());
        REQUIRE(res);
        REQUIRE(*res == 100);
    }
}

TEST_CASE("nursery many children", "[nursery]")
{
    const auto start = std::chrono::steady_clock::now();

    auto f = []() -> coral::nursery_task<> {
        const auto start2 = std::chrono::steady_clock::now();

        auto nursery = co_await coral::get_nursery();

        for (int i = 0; i < 100; ++i) {
            nursery.start(async_delay(15ms));
        }

        const auto interval2 = std::chrono::steady_clock::now() - start2;
        REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(interval2) < 15ms);
    };
    coral::sync_wait(f());
    const auto interval = std::chrono::steady_clock::now() - start;
    REQUIRE(interval >= 15ms);
    REQUIRE(interval < 50ms);
}
