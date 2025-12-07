#include "helpers.hpp"
#include <catch2/catch_test_macros.hpp>
#include <coral/sync_wait.hpp>

#include <atomic>
#include <chrono>
#include <thread>

// Helper struct for tracking construction/destruction
namespace {
struct lifetime_tracker {
    static int constructions;
    static int destructions;

    int value;

    explicit lifetime_tracker(int v)
        : value(v) {
        ++constructions;
    }
    lifetime_tracker(lifetime_tracker&& other) noexcept
        : value(other.value) {
        ++constructions;
        other.value = -1; // Mark as moved-from
    }
    ~lifetime_tracker() { ++destructions; }

    lifetime_tracker(const lifetime_tracker&) = delete;
    lifetime_tracker& operator=(const lifetime_tracker&) = delete;
    lifetime_tracker& operator=(lifetime_tracker&&) = delete;
};

int lifetime_tracker::constructions = 0;
int lifetime_tracker::destructions = 0;
} // namespace

// ============================================================================
// Basic Tests - void, T, T&
// ============================================================================

TEST_CASE("syncWait with task<int>", "[sync_wait][basic]") {
    auto task = make_int_task(42);
    int result = coral::sync_wait(std::move(task));
    REQUIRE(result == 42);
}

TEST_CASE("syncWait with task<void>", "[sync_wait][basic]") {
    auto task = make_void_task();
    REQUIRE_NOTHROW(coral::sync_wait(std::move(task)));
}

TEST_CASE("syncWait with task<T&>", "[sync_wait][basic][reference]") {
    test_int_value = 777;
    auto task = make_int_ref_task();

    int& result = coral::sync_wait(std::move(task));

    REQUIRE(&result == &test_int_value);
}

TEST_CASE("syncWait with task<const T&>", "[sync_wait][basic][reference]") {
    auto task = make_const_int_ref_task();

    const int& result = coral::sync_wait(std::move(task));

    REQUIRE(&result == &test_const_int_value);
}

TEST_CASE("syncWait with task<T*>", "[sync_wait][basic][reference]") {
    test_int_value = 777;
    auto task = make_int_ptr_task();

    int* result = coral::sync_wait(std::move(task));

    REQUIRE(result == &test_int_value);
}

TEST_CASE("syncWait with task<const T*>", "[sync_wait][basic][reference]") {
    auto task = make_const_int_ptr_task();

    const int* result = coral::sync_wait(std::move(task));

    REQUIRE(result == &test_const_int_value);
}

TEST_CASE("syncWait with task<std::string>", "[sync_wait][basic]") {
    auto chained_task = []() -> coral::task<std::string> { co_return co_await make_string_task("Hello, syncWait!"); };
    std::string result = coral::sync_wait(chained_task());
    REQUIRE(result == "Hello, syncWait!");
}

TEST_CASE("syncWait with move-only return type", "[sync_wait][move-only]") {
    auto task = make_unique_ptr_task(999);
    auto result = coral::sync_wait(std::move(task));

    REQUIRE(result != nullptr);
    REQUIRE(*result == 999);
}

// ============================================================================
// Exception Handling Tests
// ============================================================================

TEST_CASE("syncWait propagates exceptions", "[sync_wait][exception]") {
    auto task = make_throwing_int_task("Test exception");
    REQUIRE_THROWS_AS(coral::sync_wait(std::move(task)), std::runtime_error);
}

TEST_CASE("syncWait exception from nested task", "[sync_wait][exception][chaining]") {
    auto nested_throw = []() -> coral::task<int> {
        throw std::logic_error("Nested error");
        co_return 0;
    };

    auto outer_task = [&]() -> coral::task<int> {
        int val = co_await nested_throw();
        co_return val;
    };

    REQUIRE_THROWS_AS(coral::sync_wait(outer_task()), std::logic_error);
}

// ============================================================================
// Chaining Tests
// ============================================================================

TEST_CASE("syncWait with chained tasks", "[sync_wait][chaining]") {
    auto chained_task = []() -> coral::task<int> {
        int value = co_await make_int_task(42);
        co_return value * 2;
    };
    int result = coral::sync_wait(chained_task());
    REQUIRE(result == 84); // 42 * 2
}

TEST_CASE("syncWait with deep task chain", "[sync_wait][chaining]") {
    auto level3 = []() -> coral::task<int> { co_return 10; };

    auto level2 = [&]() -> coral::task<int> {
        int val = co_await level3();
        co_return val + 20;
    };

    auto level1 = [&]() -> coral::task<int> {
        int val = co_await level2();
        co_return val + 30;
    };

    int result = coral::sync_wait(level1());
    REQUIRE(result == 60); // 10 + 20 + 30
}

// Custom awaitable that resumes on a different thread
TEST_CASE("syncWait with coroutine that suspends and resumes on different thread",
    "[sync_wait][threading][cross-thread]") {

    SECTION("int type") {
        std::thread::id original_thread_id = std::this_thread::get_id();
        std::atomic<std::thread::id> resumed_thread_id;

        auto task = [&]() -> coral::task<int> {
            // We start on the main thread
            REQUIRE(std::this_thread::get_id() == original_thread_id);

            // This will suspend us, then resume on a worker thread
            co_await async_delay(std::chrono::milliseconds(10));

            // We're now running on a different thread!
            resumed_thread_id.store(std::this_thread::get_id(), std::memory_order_release);

            co_return 42;
        };

        int result = coral::sync_wait(task());

        REQUIRE(result == 42);
        // Verify we actually resumed on a different thread
        REQUIRE(resumed_thread_id.load(std::memory_order_acquire) != original_thread_id);
    }
    SECTION("void type") {
        std::thread::id original_thread_id = std::this_thread::get_id();
        std::atomic<std::thread::id> resumed_thread_id;

        auto task = [&]() -> coral::task<> {
            // We start on the main thread
            REQUIRE(std::this_thread::get_id() == original_thread_id);

            // This will suspend us, then resume on a worker thread
            co_await async_delay(std::chrono::milliseconds(10));

            // We're now running on a different thread!
            resumed_thread_id.store(std::this_thread::get_id(), std::memory_order_release);
            ;
        };

        coral::sync_wait(task());

        REQUIRE(resumed_thread_id.load(std::memory_order_acquire) != original_thread_id);
    }
}

TEST_CASE("syncWait exception safety", "[sync_wait][exception][safety]") {
    int cleanup_counter = 0;

    struct raii {
        int& counter;
        explicit raii(int& c)
            : counter(c) {}
        ~raii() { ++counter; }
    };

    auto task = [&]() -> coral::task<int> {
        raii guard{cleanup_counter};
        throw std::runtime_error("Exception!");
        co_return 0;
    };

    REQUIRE_THROWS(coral::sync_wait(task()));

    // Verify RAII cleanup happened
    REQUIRE(cleanup_counter == 1);
}

TEST_CASE("syncWait with temporary string - no dangling", "[sync_wait][lifetime][string]") {
    // This test ensures we don't have dangling references when returning strings
    auto make_string = []() -> coral::task<std::string> { co_return "Hello, World!"; };

    std::string result = coral::sync_wait(make_string());
    REQUIRE(result == "Hello, World!");
    REQUIRE(result.size() == 13);
}

TEST_CASE("syncWait with move-only types", "[sync_wait][lifetime][move-only]") {
    auto make_ptr1 = []() -> coral::task<std::unique_ptr<int>> {
        co_return std::make_unique<int>(111);
    };

    auto ptr1 = coral::sync_wait(make_ptr1());

    REQUIRE(ptr1 != nullptr);
    REQUIRE(*ptr1 == 111);
}

TEST_CASE("syncWait with vector of move-only types", "[sync_wait][lifetime][move-only]") {
    // Test returning a vector containing unique_ptrs
    auto make_vec = []() -> coral::task<std::vector<std::unique_ptr<int>>> {
        std::vector<std::unique_ptr<int>> vec;
        vec.push_back(std::make_unique<int>(1));
        vec.push_back(std::make_unique<int>(2));
        vec.push_back(std::make_unique<int>(3));
        co_return vec;
    };

    auto result = coral::sync_wait(make_vec());
    REQUIRE(result.size() == 3);
    REQUIRE(*result[0] == 1);
    REQUIRE(*result[1] == 2);
    REQUIRE(*result[2] == 3);
}

TEST_CASE("syncWait with std::optional of move-only type", "[sync_wait][lifetime][move-only]") {
    auto make_optional = []() -> coral::task<std::optional<std::unique_ptr<int>>> {
        co_return std::make_unique<int>(777);
    };

    auto result = coral::sync_wait(make_optional());
    REQUIRE(result.has_value());
    REQUIRE(*result.value() == 777);
}

TEST_CASE("syncWait destroys moved-from objects safely", "[sync_wait][lifetime][moved-from]") {
    // Track construction and destruction
    lifetime_tracker::constructions = 0;
    lifetime_tracker::destructions = 0;

    {
        auto make_tracker = []() -> coral::task<lifetime_tracker> {
            co_return lifetime_tracker{123};
        };

        auto result = coral::sync_wait(make_tracker());
        REQUIRE(result.value == 123);

        // At this point, we should have created objects and destroyed some moved-from ones
        REQUIRE(lifetime_tracker::constructions > 0);
    }

    // After result goes out of scope, all objects should be destroyed
    REQUIRE(lifetime_tracker::constructions == lifetime_tracker::destructions);
}

TEST_CASE("syncWait with exception in move constructor", "[sync_wait][lifetime][exception]") {
    struct throw_on_move {
        int value;

        throw_on_move(int v)
            : value(v) {}
        throw_on_move(const throw_on_move&) = delete;
        throw_on_move(throw_on_move&&) { throw std::runtime_error("Move failed!"); }
    };

    auto make_throw = []() -> coral::task<throw_on_move> { co_return throw_on_move{123}; };

    // This should propagate the exception from the move constructor
    REQUIRE_THROWS_AS(coral::sync_wait(make_throw()), std::runtime_error);
}
