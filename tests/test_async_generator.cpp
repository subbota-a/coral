#include <coral/async_generator.hpp>
#include <coral/single_event.hpp>
#include <coral/sync_wait.hpp>
#include <coral/task.hpp>
#include <coral/when_all.hpp>

#include "helpers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std::chrono_literals;

// ============================================================================
// Basic Async Generator Tests
// ============================================================================

coral::async_generator<int> simple_async_generator()
{
    co_yield 1;
    co_yield 2;
    co_yield 3;
}

coral::async_generator<int> async_range(int start, int end)
{
    for (int i = start; i < end; ++i) {
        co_await async_delay(1ms);
        co_yield i;
    }
}

coral::async_generator<int> empty_async_generator() { co_return; }

TEST_CASE("async_generator: basic iteration", "[async_generator]")
{
    auto test = []() -> coral::task<void> {
        auto gen = simple_async_generator();
        std::vector<int> result;

        while (auto item = co_await gen.next()) {
            result.push_back(*item);
        }

        REQUIRE(result == std::vector<int>{1, 2, 3});
    };

    coral::sync_wait(test());
}

TEST_CASE("async_generator: range-based iteration", "[async_generator]")
{
    auto test = []() -> coral::task<void> {
        auto gen = async_range(0, 5);
        std::vector<int> result;

        while (auto item = co_await gen.next()) {
            result.push_back(*item);
        }

        REQUIRE(result == std::vector<int>{0, 1, 2, 3, 4});
    };

    coral::sync_wait(test());
}

TEST_CASE("async_generator: empty generator", "[async_generator]")
{
    auto test = []() -> coral::task<void> {
        auto gen = empty_async_generator();
        std::vector<int> result;

        while (auto item = co_await gen.next()) {
            result.push_back(*item);
        }

        REQUIRE(result.empty());
    };

    coral::sync_wait(test());
}

TEST_CASE("async_generator: single element", "[async_generator]")
{
    auto single = []() -> coral::async_generator<int> { co_yield 42; };

    auto test = [&]() -> coral::task<void> {
        auto gen = single();
        auto item = co_await gen.next();
        REQUIRE(item);
        REQUIRE(*item == 42);

        auto end = co_await gen.next();
        REQUIRE(!end);
    };

    coral::sync_wait(test());
}

// ============================================================================
// Reference Type Tests
// ============================================================================

coral::async_generator<const std::string&> string_ref_async_generator()
{
    static std::string a = "hello";
    static std::string b = "world";
    co_yield a;
    co_yield b;
}

TEST_CASE("async_generator: const reference types", "[async_generator]")
{
    auto test = []() -> coral::task<void> {
        auto gen = string_ref_async_generator();
        std::vector<std::string> result;

        while (auto item = co_await gen.next()) {
            result.push_back(*item);
        }

        REQUIRE(result == std::vector<std::string>{"hello", "world"});
    };

    coral::sync_wait(test());
    coral::sync_wait(test());
}

coral::async_generator<int&> mutable_ref_async_generator(std::vector<int>& vec)
{
    for (auto& v : vec) {
        co_yield v;
    }
}

TEST_CASE("async_generator: mutable references", "[async_generator]")
{
    auto test = []() -> coral::task<void> {
        std::vector<int> vec = {1, 2, 3};
        auto gen = mutable_ref_async_generator(vec);

        while (auto item = co_await gen.next()) {
            *item *= 2;
        }

        REQUIRE(vec == std::vector<int>{2, 4, 6});
    };

    coral::sync_wait(test());
}

// ============================================================================
// Move-Only Type Tests
// ============================================================================

coral::async_generator<std::unique_ptr<int>> unique_ptr_async_generator()
{
    co_yield std::make_unique<int>(1);
    co_yield std::make_unique<int>(2);
    co_yield std::make_unique<int>(3);
}

TEST_CASE("async_generator: move-only types", "[async_generator]")
{
    auto test = []() -> coral::task<void> {
        auto gen = unique_ptr_async_generator();
        std::vector<int> values;

        while (auto item = co_await gen.next()) {
            values.push_back(**item);
        }

        REQUIRE(values == std::vector<int>{1, 2, 3});
    };

    coral::sync_wait(test());
}

// ============================================================================
// Exception Handling Tests
// ============================================================================

coral::async_generator<int> throwing_async_generator()
{
    co_yield 1;
    co_yield 2;
    throw std::runtime_error("async generator error");
    co_yield 3; // Never reached
}

TEST_CASE("async_generator: exception propagation", "[async_generator]")
{
    auto test = []() -> coral::task<void> {
        auto gen = throwing_async_generator();

        auto item1 = co_await gen.next();
        REQUIRE(item1);
        REQUIRE(*item1 == 1);

        auto item2 = co_await gen.next();
        REQUIRE(item2);
        REQUIRE(*item2 == 2);

        REQUIRE_THROWS_AS(co_await gen.next(), std::runtime_error);
    };

    coral::sync_wait(test());
}

coral::async_generator<int> throw_on_first_async()
{
    throw std::runtime_error("immediate error");
    co_yield 1; // Never reached
}

TEST_CASE("async_generator: exception on first resume", "[async_generator]")
{
    auto test = []() -> coral::task<void> {
        auto gen = throw_on_first_async();
        REQUIRE_THROWS_AS(co_await gen.next(), std::runtime_error);
    };

    coral::sync_wait(test());
}

// ============================================================================
// Early Termination Tests
// ============================================================================

coral::async_generator<int> infinite_async_generator([[maybe_unused]] std::shared_ptr<int> args)
{
    int i = 0;
    while (true) {
        co_yield i++;
    }
}

TEST_CASE("async_generator: early termination by consumer", "[async_generator]")
{
    auto test = []() -> coral::task<void> {
        auto ptr = std::make_shared<int>(100);
        auto weak = std::weak_ptr<int>(ptr);
        {
            auto gen = infinite_async_generator(std::move(ptr));
            std::vector<int> result;

            while (auto item = co_await gen.next()) {
                result.push_back(*item);
                if (result.size() >= 5) {
                    break; // Consumer stops early
                }
            }
            REQUIRE(result == std::vector<int>{0, 1, 2, 3, 4});
        }
        // Generator is destroyed here
        REQUIRE(weak.expired());
    };

    coral::sync_wait(test());
}

coral::async_generator<int> generator_with_raii([[maybe_unused]] std::shared_ptr<int> args)
{
    co_yield 1;
}

TEST_CASE("async_generator: RAII cleanup on early destruction", "[async_generator]")
{
    auto test = [&]() -> coral::task<void> {
        auto ptr = std::make_shared<int>(100);
        auto weak = std::weak_ptr<int>(ptr);

        auto gen = generator_with_raii(std::move(ptr));
        auto item = co_await gen.next();
        REQUIRE(item);
        REQUIRE(*item == 1);
        REQUIRE_FALSE(co_await gen.next());
        REQUIRE_FALSE(weak.expired());
    };

    coral::sync_wait(test());
}

// ============================================================================
// Move Semantics Tests
// ============================================================================

TEST_CASE("async_generator: move constructor", "[async_generator]")
{
    auto test = []() -> coral::task<void> {
        auto gen1 = simple_async_generator();
        auto item1 = co_await gen1.next();
        REQUIRE(*item1 == 1);

        auto gen2 = std::move(gen1);
        auto item2 = co_await gen2.next();
        REQUIRE(*item2 == 2);

        auto item3 = co_await gen2.next();
        REQUIRE(*item3 == 3);

        auto end = co_await gen2.next();
        REQUIRE(!end);
    };

    coral::sync_wait(test());
}

TEST_CASE("async_generator: move assignment", "[async_generator]")
{
    auto test = []() -> coral::task<void> {
        auto gen1 = simple_async_generator();
        auto gen2 = async_range(10, 15);

        gen2 = std::move(gen1);

        std::vector<int> result;
        while (auto item = co_await gen2.next()) {
            result.push_back(*item);
        }

        REQUIRE(result == std::vector<int>{1, 2, 3});
    };

    coral::sync_wait(test());
}

// ============================================================================
// Yielding lvalues (copy to frame)
// ============================================================================

coral::async_generator<std::string> yield_lvalue_generator()
{
    std::string x("100");
    co_yield x; // lvalue - should copy to frame
    REQUIRE(x == "100");
    x = "200";
    co_yield x; // different value
}

TEST_CASE("async_generator: yield lvalue copies to frame", "[async_generator]")
{
    auto test = []() -> coral::task<void> {
        auto gen = yield_lvalue_generator();
        std::vector<std::string> result;

        while (auto item = co_await gen.next()) {
            result.push_back(*item);
        }

        REQUIRE(result == std::vector<std::string>{"100", "200"});
    };

    coral::sync_wait(test());
}
