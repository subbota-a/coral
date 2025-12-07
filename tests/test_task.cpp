#include <coral/task.hpp>

#include "helpers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

// ============================================================================
// Test Helpers
// ============================================================================

// Task that awaits another task
coral::task<int> chained_task()
{
    int value = co_await make_int_task(42);
    co_return value * 2;
}

// Task with multiple co_await
coral::task<int> multi_await_task()
{
    int a = co_await make_int_task(42);
    int b = co_await make_int_task(42);
    co_return a + b;
}

// Task that awaits void task
coral::task<int> await_void_task()
{
    co_await make_void_task();
    co_return 123;
}

// Task that modifies a reference
coral::task<> modify_reference_task(int& value)
{
    value = 999;
    co_return;
}

// Deep chain of tasks
coral::task<int> deep_chain_level3() { co_return 10; }

coral::task<int> deep_chain_level2()
{
    int val = co_await deep_chain_level3();
    co_return val + 20;
}

coral::task<int> deep_chain_level1()
{
    int val = co_await deep_chain_level2();
    co_return val + 30;
}

// Execute a task synchronously and return the result
template <typename T>
decltype(auto) sync_wait(coral::task<T>&& task)
{
    auto awaiter = std::move(task).operator co_await();
    auto handle = awaiter.await_suspend(std::noop_coroutine());

    handle.resume();

    return awaiter.await_resume();
}

// ============================================================================
// Test Cases
// ============================================================================

TEST_CASE("task<int> returns correct value", "[task][basic]")
{
    auto task = make_int_task(42);

    const auto& result = sync_wait(std::move(task));
    REQUIRE(result == 42);
}

TEST_CASE("task<void> completes successfully", "[task][basic]")
{
    auto task = make_void_task();

    REQUIRE_NOTHROW(sync_wait(std::move(task)));
}

TEST_CASE("task<T&> returns reference", "[task][reference]")
{
    test_int_value = 777;
    auto task = make_int_ref_task();

    int& result = sync_wait(std::move(task));

    REQUIRE(&result == &test_int_value);
    REQUIRE(result == 777);

    // Verify we can modify through the reference
    result = 888;
    REQUIRE(test_int_value == 888);
}

TEST_CASE("task<std::string> returns string", "[task][string]")
{
    auto task = make_string_task("Hello, Coral!");

    std::string result = sync_wait(std::move(task));
    REQUIRE(result == "Hello, Coral!");
}

TEST_CASE("co_await with auto&& binding does not dangle", "[task][string][lifetime]")
{
    // This test verifies that await_resume() returns by value (not T&&)
    // so that auto&& binding doesn't create dangling references
    auto consumer = []() -> coral::task<std::string> {
        // co_await now returns T (by value), not T&&
        // auto&& binds to the prvalue, which is safe
        auto&& result = co_await make_string_task("Hello, Coral!");

        // The inner task (stringTask) is destroyed here
        // But result is a copy, not a reference to the destroyed promise
        // So accessing it is safe
        co_return std::string(result);
    };

    std::string final_result = sync_wait(consumer());
    REQUIRE(final_result == "Hello, Coral!");
}

TEST_CASE("task is move-only", "[task][movable]")
{
    auto task1 = make_int_task(42);
    auto task2 = std::move(task1);

    int result = sync_wait(std::move(task2));
    REQUIRE(result == 42);
}

TEST_CASE("task propagates exceptions", "[task][exception]")
{
    auto task = make_throwing_int_task("Test exception");

    REQUIRE_THROWS_AS(sync_wait(std::move(task)), std::runtime_error);
}

TEST_CASE("task can await another task", "[task][chaining]")
{
    auto task = chained_task();

    int result = sync_wait(std::move(task));
    REQUIRE(result == 84); // 42 * 2
}

TEST_CASE("task can await multiple tasks", "[task][chaining]")
{
    auto task = multi_await_task();

    int result = sync_wait(std::move(task));
    REQUIRE(result == 84); // 42 + 42
}

TEST_CASE("task can await void task", "[task][chaining]")
{
    auto task = await_void_task();

    int result = sync_wait(std::move(task));
    REQUIRE(result == 123);
}

TEST_CASE("deep task chain works correctly", "[task][chaining]")
{
    auto task = deep_chain_level1();

    int result = sync_wait(std::move(task));
    REQUIRE(result == 60); // 10 + 20 + 30
}

TEST_CASE("task can modify parameter by reference", "[task][reference]")
{
    int value = 0;
    auto task = modify_reference_task(value);

    sync_wait(std::move(task));
    REQUIRE(value == 999);
}

TEST_CASE("multiple tasks can be created from same function", "[task][multiple]")
{
    auto task1 = make_int_task(42);
    auto task2 = make_int_task(42);

    int result1 = sync_wait(std::move(task1));
    int result2 = sync_wait(std::move(task2));

    REQUIRE(result1 == 42);
    REQUIRE(result2 == 42);
}

TEST_CASE("task exception doesn't affect other tasks", "[task][exception][isolation]")
{
    auto good_task = make_int_task(42);
    auto bad_task = make_throwing_int_task("Test exception");

    REQUIRE_THROWS(sync_wait(std::move(bad_task)));

    int result = sync_wait(std::move(good_task));
    REQUIRE(result == 42);
}

TEST_CASE("task with const reference return", "[task][reference][const]")
{
    auto task = make_const_int_ref_task();
    const int& result = sync_wait(std::move(task));

    REQUIRE(&result == &test_const_int_value);
    REQUIRE(result == 100);
}

TEST_CASE("task exception from nested task propagates", "[task][exception][chaining]")
{
    auto outer_task = []() -> coral::task<int> {
        int val = co_await make_throwing_int_task("Nested error");
        co_return val;
    };

    REQUIRE_THROWS_AS(sync_wait(outer_task()), std::runtime_error);
}

TEST_CASE("task can return move-only type", "[task][move-only]")
{
    auto task = make_unique_ptr_task(123);
    auto result = sync_wait(std::move(task));

    REQUIRE(result != nullptr);
    REQUIRE(*result == 123);
}

// TEST_CASE("task awaiter works with const lvalue", "[task][awaiter]") {
//     const auto task = simpleIntTask();

//     // Test that we can get an awaiter from const lvalue
//     auto awaiter = task.operator co_await();
//     REQUIRE_FALSE(awaiter.await_ready());
// }

TEST_CASE("empty task chain", "[task][edge-case]")
{
    auto passthrough = []() -> coral::task<int> { co_return co_await make_int_task(42); };

    int result = sync_wait(passthrough());
    REQUIRE(result == 42);
}

TEST_CASE("coroutine destroys after co_await")
{
    struct coro_arg {
        bool* flag{nullptr};
        coro_arg(bool* f)
            : flag{f}
        {
        }
        coro_arg(coro_arg&& t)
            : flag{std::exchange(t.flag, nullptr)}
        {
        }
        coro_arg(const coro_arg&) = delete;
        ~coro_arg()
        {
            if (flag) {
                *flag = true;
            }
        }
    };
    auto t = [](coro_arg arg) -> coral::task<> { co_return; };

    bool flag = false;
    auto f = [&flag](coral::task<> subtask) -> coral::task<> {
        co_await std::move(subtask);
        REQUIRE(flag == true);
    };

    sync_wait(f(t(coro_arg{&flag})));
}