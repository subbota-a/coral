// Suppress GCC false positive warnings about structured bindings in coroutines.
// GCC's uninitialized variable analysis incorrectly flags structured bindings
// as "may be used uninitialized" when used inside coroutine frames,
// particularly with multiple co_await expressions. This is a known GCC bug (see
// GCC Bug #95516). The warning disappears when using std::get instead of
// structured bindings, confirming it's a false positive specific to the
// interaction between structured bindings and coroutine state machine
// transformations.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

#include <coral/sync_wait.hpp>
#include <coral/when_all_complete.hpp>

#include "helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <chrono>

using namespace std::chrono_literals;
// ============================================================================
// Synchronous Completion Tests - Different Return Types
// ============================================================================

TEST_CASE("when_all_complete with single int task", "[when_all_complete][sync][basic]")
{
    auto [r1] = coral::sync_wait(coral::when_all_complete(make_int_task(42)));
    REQUIRE(r1.value() == 42);
}

TEST_CASE("when_all_complete with two int tasks", "[when_all_complete][sync][basic]")
{
    auto [r1, r2] = coral::sync_wait(coral::when_all_complete(make_int_task(10), make_int_task(20)));
    REQUIRE(r1.value() + r2.value() == 30);
}

TEST_CASE("when_all_complete with three int tasks", "[when_all_complete][sync][basic]")
{
    auto [r1, r2, r3] =
        coral::sync_wait(coral::when_all_complete(make_int_task(1), make_int_task(2), make_int_task(3)));
    REQUIRE(r1.value() + r2.value() + r3.value() == 6);
}

TEST_CASE("when_all_complete with mixed types: int, string", "[when_all_complete][sync][mixed]")
{
    auto [r1, r2] = coral::sync_wait(coral::when_all_complete(make_int_task(42), make_string_task("hello")));

    int int_val = r1.value();
    std::string str_val = r2.value();

    REQUIRE(str_val + std::to_string(int_val) == "hello42");
}

TEST_CASE("when_all_complete with int reference", "[when_all_complete][sync][reference]")
{
    test_int_value = 123;

    auto [r1] = coral::sync_wait(coral::when_all_complete(make_int_ref_task()));
    int& ref = r1.value();

    REQUIRE(&ref == &test_int_value);
    ref = 456;

    REQUIRE(test_int_value == 456);
}

TEST_CASE("when_all_complete with const int reference", "[when_all_complete][sync][reference]")
{
    auto [r1] = coral::sync_wait(coral::when_all_complete(make_const_int_ref_task()));
    const int& ref = r1.value();

    REQUIRE(&ref == &test_const_int_value);
    REQUIRE(ref == 100);
}

TEST_CASE("when_all_complete with int pointer", "[when_all_complete][sync][pointer]")
{
    test_int_value = 777;

    auto [r1] = coral::sync_wait(coral::when_all_complete(make_int_ptr_task()));
    int* ptr = r1.value();

    REQUIRE(ptr == &test_int_value);
    *ptr = 888;

    REQUIRE(test_int_value == 888);
}

TEST_CASE("when_all_complete with const int pointer", "[when_all_complete][sync][pointer]")
{
    auto [r1] = coral::sync_wait(coral::when_all_complete(make_const_int_ptr_task()));
    const int* ptr = r1.value();

    REQUIRE(ptr == &test_const_int_value);
    REQUIRE(*ptr == 100);
}

TEST_CASE("when_all_complete with std::unique_ptr", "[when_all_complete][sync][move_only]")
{
    auto [r1] = coral::sync_wait(coral::when_all_complete(make_unique_ptr_task(999)));
    std::unique_ptr<int> ptr = std::move(r1).value();

    REQUIRE(ptr != nullptr);
    REQUIRE(*ptr == 999);
}

TEST_CASE("when_all_complete with multiple std::unique_ptr", "[when_all_complete][sync][move_only]")
{
    auto [r1, r2, r3] = coral::sync_wait(coral::when_all_complete(
        make_unique_ptr_task(10), make_unique_ptr_task(20), make_unique_ptr_task(30)));

    std::unique_ptr<int> p1 = std::move(r1).value();
    std::unique_ptr<int> p2 = std::move(r2).value();
    std::unique_ptr<int> p3 = std::move(r3).value();

    REQUIRE(*p1 + *p2 + *p3 == 60);
}

TEST_CASE("when_all_complete with std::string", "[when_all_complete][sync][string]")
{
    auto [r1, r2] =
        coral::sync_wait(coral::when_all_complete(make_string_task("Hello"), make_string_task(" World")));

    REQUIRE(r1.value() + r2.value() == "Hello World");
}

TEST_CASE("when_all_complete with all different types", "[when_all_complete][sync][mixed]")
{
    test_int_value = 5;

    auto [r1, r2, r3, r4, r5, r6, r7] = coral::sync_wait(coral::when_all_complete(make_int_task(42), // int
        make_int_ref_task(),                                                                // int&
        make_int_ptr_task(),                                                                // int*
        make_const_int_ptr_task(), // const int*
        make_const_int_ref_task(), // const int&
        make_unique_ptr_task(99),  // std::unique_ptr<int>
        make_string_task("test")   // std::string
        ));

    int val1 = r1.value();                             // 42
    int& val2 = r2.value();                            // 5
    int* val3 = r3.value();                            // &test_int_value (5)
    const int* val4 = r4.value();                      // &test_const_int_value (100)
    const int& val5 = r5.value();                      // 100
    std::unique_ptr<int> val6 = std::move(r6).value(); // 99
    std::string val7 = r7.value();                     // "test"

    int sum = val1 + val2 + *val3 + *val4 + val5 + *val6;

    REQUIRE(val7 + ":" + std::to_string(sum) == "test:351"); // 42 + 5 + 5 + 100 + 100 + 99 = 351
}

// ============================================================================
// Asynchronous Completion Tests
// ============================================================================

TEST_CASE("when_all_complete with two async tasks", "[when_all_complete][async]")
{
    auto start = std::chrono::steady_clock::now();

    auto [r1, r2] =
        coral::sync_wait(coral::when_all_complete(make_delayed_int_task(10, std::chrono::milliseconds(20)),
            make_delayed_int_task(20, std::chrono::milliseconds(30))));

    auto end = std::chrono::steady_clock::now();

    REQUIRE(r1.value() + r2.value() == 30);

    // Should complete in roughly max(20ms, 30ms) = ~30ms, not 50ms (sequential)
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    REQUIRE(duration < std::chrono::milliseconds(45)); // Some tolerance
}

TEST_CASE("when_all_complete with multiple async tasks of different durations", "[when_all_complete][async]")
{
    auto start = std::chrono::steady_clock::now();

    auto [r1, r2, r3, r4] =
        coral::sync_wait(coral::when_all_complete(make_delayed_int_task(1, std::chrono::milliseconds(10)),
            make_delayed_int_task(2, std::chrono::milliseconds(20)),
            make_delayed_int_task(3, std::chrono::milliseconds(15)),
            make_delayed_int_task(4, std::chrono::milliseconds(5))));

    auto end = std::chrono::steady_clock::now();

    REQUIRE(r1.value() + r2.value() + r3.value() + r4.value() == 10);

    // Should complete in roughly max(10, 20, 15, 5) = ~20ms
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    REQUIRE(duration < std::chrono::milliseconds(35));
}

TEST_CASE("when_all_complete with mix of sync and async tasks", "[when_all_complete][mixed][async]")
{
    auto [r1, r2, r3] = coral::sync_wait(coral::when_all_complete(make_int_task(10), // sync
        make_delayed_int_task(20, std::chrono::milliseconds(15)),               // async
        make_int_task(30)                                                   // sync
        ));

    REQUIRE(r1.value() + r2.value() + r3.value() == 60);
}

// ============================================================================
// Exception Handling Tests
// ============================================================================

TEST_CASE("when_all_complete with one throwing task", "[when_all_complete][exception]")
{
    auto [r1, r2] =
        coral::sync_wait(coral::when_all_complete(make_int_task(42), make_throwing_int_task("error")));

    REQUIRE(r1.has_value());
    REQUIRE_FALSE(r2.has_value());
    REQUIRE(r1.value() == 42);
    REQUIRE_THROWS_AS(r2.value(), std::runtime_error);
}

TEST_CASE("when_all_complete with two throwing tasks", "[when_all_complete][exception]")
{
    auto [r1, r2, r3] = coral::sync_wait(coral::when_all_complete(
        make_throwing_int_task("error1"), make_int_task(42), make_throwing_int_task("error2")));

    REQUIRE_FALSE(r1.has_value());
    REQUIRE(r2.has_value());
    REQUIRE_FALSE(r3.has_value());
    REQUIRE_THROWS_AS(r1.value(), std::runtime_error);
}

TEST_CASE("when_all_completeexception in first task", "[when_all_complete][exception]")
{
    auto [r1, r2] =
        coral::sync_wait(coral::when_all_complete(make_throwing_int_task("first"), make_int_task(10)));

    REQUIRE_FALSE(r1.has_value());
    REQUIRE(r2.has_value());
    REQUIRE_THROWS_AS(r1.value(), std::runtime_error);
    REQUIRE(r2.value() == 10);
}

TEST_CASE("when_all_completeexception in last task", "[when_all_complete][exception]")
{
    auto [r1, r2] =
        coral::sync_wait(coral::when_all_complete(make_int_task(10), make_throwing_int_task("last")));

    REQUIRE(r1.has_value());
    REQUIRE_FALSE(r2.has_value());
    REQUIRE(r1.value() == 10);
    REQUIRE_THROWS_AS(r2.value(), std::runtime_error);
}

TEST_CASE("when_all_completeexception in middle task", "[when_all_complete][exception]")
{
    auto [r1, r2, r3] = coral::sync_wait(
        coral::when_all_complete(make_int_task(10), make_throwing_int_task("middle"), make_int_task(20)));

    REQUIRE(r1.has_value());
    REQUIRE_FALSE(r2.has_value());
    REQUIRE(r3.has_value());
    REQUIRE(r1.value() == 10);
    REQUIRE_THROWS_AS(r2.value(), std::runtime_error);
    REQUIRE(r3.value() == 20);
}

TEST_CASE("when_all_complete with async task that throws", "[when_all_complete][exception][async]")
{
    auto [r1, r2] =
        coral::sync_wait(coral::when_all_complete(make_delayed_int_task(10, std::chrono::milliseconds(5)),
            make_delayed_throwing_void_task("async error", std::chrono::milliseconds(10))));

    REQUIRE(r1.has_value());
    REQUIRE_FALSE(r2.has_value());
    REQUIRE(r1.value() == 10);
    REQUIRE_THROWS_AS(r2.value(), std::runtime_error);
}

TEST_CASE("when_all_completetwo exceptions thrown simultaneously", "[when_all_complete][exception][multiple]")
{
    auto [r1, r2] = coral::sync_wait(
        coral::when_all_complete(make_throwing_int_task("exception1"), make_throwing_int_task("exception2")));

    REQUIRE_FALSE(r1.has_value());
    REQUIRE_FALSE(r2.has_value());

    // Both should have exceptions
    REQUIRE_THROWS_AS(r1.value(), std::runtime_error);
    REQUIRE_THROWS_AS(r2.value(), std::runtime_error);
}

TEST_CASE("when_all_completenested - all inside all", "[when_all_complete][nested]")
{
    auto inner1 = []() -> coral::task<int> {
        auto [r1, r2] = co_await coral::when_all_complete(make_int_task(10), make_int_task(20));
        co_return r1.value() + r2.value();
    };

    auto inner2 = []() -> coral::task<int> {
        auto [r1, r2] = co_await coral::when_all_complete(make_int_task(30), make_int_task(40));
        co_return r1.value() + r2.value();
    };

    auto [r1, r2] = coral::sync_wait(coral::when_all_complete(inner1(), inner2()));

    REQUIRE(r1.value() + r2.value() == 100); // (10+20) + (30+40) = 100
}

TEST_CASE("when_all_complete with task that awaits another task", "[when_all_complete][chaining]")
{
    auto chained_task = []() -> coral::task<int> {
        int a = co_await make_int_task(5);
        int b = co_await make_int_task(10);
        co_return a + b;
    };

    auto [r1, r2] = coral::sync_wait(coral::when_all_complete(chained_task(), make_int_task(100)));

    REQUIRE(r1.value() + r2.value() == 115); // (5+10) + 100 = 115
}

TEST_CASE("when_all_complete with Result error checking", "[when_all_complete]")
{
    auto [r1, r2] = coral::sync_wait(coral::when_all_complete(make_int_task(10), make_int_task(20)));

    REQUIRE(r1.has_value());
    REQUIRE(r2.has_value());
    REQUIRE(r1.value() == 10);
    REQUIRE(r2.value() == 20);
}

TEST_CASE("when_all_complete ranges int", "[when_all_complete]")
{
    std::vector<coral::task<int>> tasks;
    tasks.push_back(make_int_task(10));
    tasks.push_back(make_int_task(20));

    auto r = coral::sync_wait(coral::when_all_complete(std::move(tasks)));

    REQUIRE(r.size() == 2);
    REQUIRE(r[0].has_value());
    REQUIRE(r[1].has_value());
    REQUIRE(r[0].value() == 10);
    REQUIRE(r[1].value() == 20);
}

TEST_CASE("when_all_complete ranges int&", "[when_all_complete]")
{
    std::vector<coral::task<int&>> tasks;
    tasks.push_back(make_int_ref_task());

    auto r = coral::sync_wait(coral::when_all_complete(std::move(tasks)));

    REQUIRE(r.size() == 1);
    REQUIRE(r[0].has_value());
    REQUIRE(r[0].value() == test_int_value);
    REQUIRE(&r[0].value() == &test_int_value);
}

TEST_CASE("when_all_complete ranges with async task that throws", "[when_all_complete][exception][async]")
{
    std::vector<coral::task<void>> tasks;
    tasks.push_back(make_void_task());
    tasks.push_back(make_delayed_throwing_void_task("async error", std::chrono::milliseconds(10)));

    auto r = coral::sync_wait(coral::when_all_complete(tasks));

    REQUIRE(r.size() == 2);

    REQUIRE(r[0].has_value());
    REQUIRE_NOTHROW(r[0].value());

    REQUIRE(!r[1].has_value());
    REQUIRE_THROWS_WITH(r[1].value(), "async error");
}


TEST_CASE("when_all_complete ranges empty", "[when_all_complete]")
{
    std::vector<coral::task<int>> tasks;

    auto r = coral::sync_wait(coral::when_all_complete(std::move(tasks)));

    REQUIRE(r.size() == 0);
}

TEST_CASE("when_all_complete task alive")
{
    auto t = []()->coral::task<>{
        auto st = []()->coral::task<>{
            co_await async_delay(1ms);
        };
        co_await coral::when_all_complete(st());
    };
    coral::sync_wait(t());
}