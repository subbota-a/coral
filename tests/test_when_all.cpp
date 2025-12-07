// Suppress GCC false positive warnings about structured bindings in coroutines.
// GCC's uninitialized variable analysis incorrectly flags structured bindings
// as "may be used uninitialized" when used inside coroutine frames,
// particularly with multiple co_await expressions. This is a known GCC bug (see
// GCC Bug #95516). The warning disappears when using std::get instead of
// structured bindings, confirming it's a false positive specific to the
// interaction between structured bindings and coroutine state machine
// transformations.
#include <coral/detail/config.hpp>
#include <coral/sync_wait.hpp>
#include <coral/when_all.hpp>

#include "coral/task.hpp"
#include "helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <array>
#include <chrono>
#include <forward_list>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <variant>

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

using namespace std::chrono_literals;
// ============================================================================
// Synchronous Completion Tests - Different Return Types
// ============================================================================

TEST_CASE("when_all with single int task", "[when_all][sync][basic]")
{
    auto [r1] = coral::sync_wait(coral::when_all(make_int_task(42)));
    REQUIRE(r1 == 42);
}

TEST_CASE("when_all with two int tasks", "[when_all][sync][basic]")
{
    auto [r1, r2] = coral::sync_wait(coral::when_all(make_int_task(10), make_int_task(20)));
    REQUIRE(r1 + r2 == 30);
}

TEST_CASE("when_all with mixed types: int, string", "[when_all][sync][mixed]")
{
    auto [r1, r2] =
        coral::sync_wait(coral::when_all(make_int_task(42), make_string_task("hello")));

    REQUIRE(r2 + std::to_string(r1) == "hello42");
}

TEST_CASE("when_all with int reference", "[when_all][sync][reference]")
{
    test_int_value = 123;

    auto [r1] = coral::sync_wait(coral::when_all(make_int_ref_task()));

    REQUIRE(&r1 == &test_int_value);
    r1 = 456;

    REQUIRE(test_int_value == 456);
}

TEST_CASE("when_all with const int reference", "[when_all][sync][reference]")
{
    auto [r1] = coral::sync_wait(coral::when_all(make_const_int_ref_task()));

    REQUIRE(&r1 == &test_const_int_value);
    REQUIRE(r1 == 100);
}

TEST_CASE("when_all with int pointer", "[when_all][sync][pointer]")
{
    test_int_value = 777;

    auto [ptr] = coral::sync_wait(coral::when_all(make_int_ptr_task()));

    REQUIRE(ptr == &test_int_value);
    *ptr = 888;

    REQUIRE(test_int_value == 888);
}

TEST_CASE("when_all with const int pointer", "[when_all][sync][pointer]")
{
    auto [ptr] = coral::sync_wait(coral::when_all(make_const_int_ptr_task()));

    REQUIRE(ptr == &test_const_int_value);
    REQUIRE(*ptr == 100);
}

TEST_CASE("when_all with std::unique_ptr", "[when_all][sync][move_only]")
{
    auto [ptr] = coral::sync_wait(coral::when_all(make_unique_ptr_task(999)));

    REQUIRE(ptr != nullptr);
    REQUIRE(*ptr == 999);
}

TEST_CASE("when_all with multiple std::unique_ptr", "[when_all][sync][move_only]")
{
    auto [r1, r2, r3] = coral::sync_wait(coral::when_all(
        make_unique_ptr_task(10), make_unique_ptr_task(20), make_unique_ptr_task(30)));

    std::unique_ptr<int> p1 = std::move(r1);
    std::unique_ptr<int> p2 = std::move(r2);
    std::unique_ptr<int> p3 = std::move(r3);

    REQUIRE(*p1 + *p2 + *p3 == 60);
}

TEST_CASE("when_all with std::string", "[when_all][sync][string]")
{
    auto [r1, r2] = coral::sync_wait(
        coral::when_all(make_string_task("Hello"), make_string_task(" World")));

    REQUIRE(r1 + r2 == "Hello World");
}

TEST_CASE("when_all with all different types", "[when_all][sync][mixed]")
{
    test_int_value = 5;

    auto result = coral::sync_wait(coral::when_all(make_int_task(42),
        make_int_ref_task(),
        make_int_ptr_task(),
        make_const_int_ptr_task(),
        make_const_int_ref_task(),
        make_unique_ptr_task(99),
        make_string_task("test"),
        make_void_task()));
    using expected_result_t = std::tuple<int,
        int&,
        int*,
        const int*,
        const int&,
        std::unique_ptr<int>,
        std::string,
        std::monostate>;
    STATIC_REQUIRE(std::is_same_v<decltype(result), expected_result_t>);
}

// ============================================================================
// Asynchronous Completion Tests
// ============================================================================

TEST_CASE("when_all with two async tasks", "[when_all][async]")
{
    auto start = std::chrono::steady_clock::now();

    auto [r1, r2] = coral::sync_wait(
        coral::when_all(make_delayed_int_task(10, std::chrono::milliseconds(30)),
            make_delayed_int_task(20, std::chrono::milliseconds(30))));

    auto end = std::chrono::steady_clock::now();

    REQUIRE(r1 + r2 == 30);

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    REQUIRE(duration < std::chrono::milliseconds(45)); // Some tolerance
}

// ============================================================================
// Exception Handling Tests - WITHOUT stop_source
// ============================================================================

TEST_CASE("when_all with one throwing task - no stop_source",
    "[when_all][exception]")
{
    auto run_task = [](bool& flag) -> coral::task<int> {
        flag = true;
        co_return 100;
    };
    auto never_run_task = []() -> coral::task<int> {
        FAIL("This task is not suppose to be run");
        co_return 100;
    };
    SECTION("first throws")
    {
        REQUIRE_THROWS_AS(coral::sync_wait(coral::when_all(
                              make_throwing_int_task("first"), never_run_task())),
            std::runtime_error);
    }
    SECTION("last throws")
    {
        auto flag = false;
        REQUIRE_THROWS_AS(coral::sync_wait(coral::when_all(
                              run_task(flag), make_throwing_int_task("error"))),
            std::runtime_error);
        REQUIRE(flag);
    }
    SECTION("middle throws")
    {
        auto flag = false;
        REQUIRE_THROWS_AS(coral::sync_wait(coral::when_all(
                              run_task(flag), make_throwing_int_task("middle"), never_run_task())),
            std::runtime_error);
        REQUIRE(flag);
    }
    SECTION("middle throws 2")
    {
        auto flag = false;
        REQUIRE_THROWS_AS(coral::sync_wait(coral::when_all(run_task(flag),
                              make_throwing_int_task("middle"),
                              never_run_task(),
                              never_run_task())),
            std::runtime_error);
        REQUIRE(flag);
    }
    SECTION("all throws")
    {
        REQUIRE_THROWS_WITH(
            coral::sync_wait(coral::when_all(
                make_throwing_int_task("error1"), make_throwing_int_task("error2"))),
            "error1");
    }
    SECTION("first throws async")
    {
        REQUIRE_THROWS_WITH(
            coral::sync_wait(coral::when_all(
                make_delayed_throwing_void_task("async error", 10ms), make_int_task(10))),
            "async error");
    }
    SECTION("last throws async")
    {
        REQUIRE_THROWS_WITH(coral::sync_wait(coral::when_all(make_int_task(10),
                                make_delayed_throwing_void_task("async error", 10ms))),
            "async error");
    }
    SECTION("both throws async 1")
    {
        REQUIRE_THROWS_WITH(coral::sync_wait(coral::when_all(
                                make_delayed_throwing_void_task("first async error", 1ms),
                                make_delayed_throwing_void_task("second async error", 20ms))),
            "first async error");
    }
    SECTION("both throws async 2")
    {
        REQUIRE_THROWS_WITH(coral::sync_wait(coral::when_all(
                                make_delayed_throwing_void_task("first async error", 20ms),
                                make_delayed_throwing_void_task("second async error", 1ms))),
            "second async error");
    }
}

TEST_CASE("when_all with stop_source - no exception doesn't trigger stop",
    "[when_all][stop_source]")
{
    std::stop_source stop_source;

    REQUIRE_FALSE(stop_source.stop_requested());

    auto [r1, r2] = coral::sync_wait(
        coral::when_all(stop_source, make_int_task(10), make_int_task(20)));

    REQUIRE(r1 + r2 == 30);
    // stop_source should NOT have been triggered
    REQUIRE_FALSE(stop_source.stop_requested());
}

TEST_CASE("when_all with stop_source - exception triggers stop for long-running tasks")
{
    std::stop_source stop_source;
    auto token = stop_source.get_token();

    auto start = std::chrono::steady_clock::now();

    REQUIRE_THROWS_AS(coral::sync_wait(coral::when_all(stop_source,
                          make_stoppable_task(token, 10, std::chrono::milliseconds(100)),
                          make_throwing_int_task("error"))),
        std::runtime_error);

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    REQUIRE(stop_source.stop_requested());
    REQUIRE(duration < std::chrono::milliseconds(30));
}

TEST_CASE("when_all nested - when_all inside when_all",
    "[when_all][nested]")
{
    auto inner1 = []() -> coral::task<int> {
        auto [r1, r2] = co_await coral::when_all(make_int_task(10), make_int_task(20));
        co_return r1 + r2;
    };

    auto inner2 = []() -> coral::task<int> {
        auto [r1, r2] = co_await coral::when_all(make_int_task(30), make_int_task(40));
        co_return r1 + r2;
    };

    auto [r1, r2] = coral::sync_wait(coral::when_all(inner1(), inner2()));

    REQUIRE(r1 + r2 == 100); // (10+20) + (30+40) = 100
}

TEST_CASE("when_all with void tasks")
{
    int counter = 0;

    auto void_task_1 = [&counter]() -> coral::task<void> {
        counter += 10;
        co_return;
    };

    auto void_task_2 = [&counter]() -> coral::task<void> {
        counter += 20;
        co_return;
    };

    coral::sync_wait(coral::when_all(void_task_1(), void_task_2()));

    REQUIRE(counter == 30);
}

TEST_CASE("when_all with mixed void and non-void tasks")
{
    int counter = 0;

    auto void_task = [&counter]() -> coral::task<void> {
        counter += 5;
        co_return;
    };

    auto [v1, v_void, v2] = coral::sync_wait(
        coral::when_all(make_int_task(10), void_task(), make_int_task(20)));

    REQUIRE(v1 == 10);
    REQUIRE(v2 == 20);
    REQUIRE(counter == 5);
    static_assert(std::is_same_v<decltype(v_void), std::monostate>);
}

TEST_CASE("when_all range types")
{
    SECTION("empty void")
    {
        std::vector<coral::task<void>> tasks;
        using type = decltype(coral::sync_wait(coral::when_all(std::move(tasks))));
        STATIC_REQUIRE(std::is_void_v<type>);
        coral::sync_wait(coral::when_all(std::move(tasks)));
    }
    SECTION("voids")
    {
        std::vector<coral::task<void>> tasks;
        tasks.push_back(make_void_task());
        tasks.push_back(make_void_task());
        coral::sync_wait(coral::when_all(std::move(tasks)));
    }
    SECTION("int")
    {
        std::vector<coral::task<int>> tasks;
        tasks.push_back(make_int_task(1));
        tasks.push_back(make_int_task(2));
        auto res = coral::sync_wait(coral::when_all(std::move(tasks)));
        STATIC_REQUIRE(std::is_same_v<decltype(res), std::vector<int>>);

        REQUIRE(res.size() == 2);
        REQUIRE(res[0] == 1);
        REQUIRE(res[1] == 2);
    }
    SECTION("int&")
    {
        std::vector<coral::task<int&>> tasks;
        tasks.push_back(make_int_ref_task());
        auto res = coral::sync_wait(coral::when_all(std::move(tasks)));
        STATIC_REQUIRE(std::is_same_v<decltype(res), std::vector<std::reference_wrapper<int>>>);

        REQUIRE(res.size() == 1);
        REQUIRE(&res[0].get() == &test_int_value);
    }
    SECTION("unique_ptr")
    {
        std::vector<coral::task<std::unique_ptr<int>>> tasks;
        tasks.push_back(make_unique_ptr_task(100));
        auto res = coral::sync_wait(coral::when_all(std::move(tasks)));
        STATIC_REQUIRE(std::is_same_v<decltype(res), std::vector<std::unique_ptr<int>>>);

        REQUIRE(res.size() == 1);
        REQUIRE(res[0].get() != nullptr);
        REQUIRE(*res[0] == 100);
    }
}

TEST_CASE("when_all range with two async tasks")
{
    std::forward_list<coral::task<int>> tasks;
    tasks.push_front(make_delayed_int_task(10, std::chrono::milliseconds(50)));
    tasks.push_front(make_delayed_int_task(20, std::chrono::milliseconds(50)));

    auto start = std::chrono::steady_clock::now();

    auto r = coral::sync_wait(coral::when_all(std::move(tasks)));

    auto end = std::chrono::steady_clock::now();

    REQUIRE(r.size() == 2);
    REQUIRE(r[0] == 20);
    REQUIRE(r[1] == 10);

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    REQUIRE(duration < std::chrono::milliseconds(100)); // Some tolerance
}

TEST_CASE("when_all range throwing task - no stop_source")
{
    auto run_task = [](bool& flag) -> coral::task<int> {
        flag = true;
        co_return 100;
    };
    auto never_run_task = []() -> coral::task<int> {
        FAIL("This task is not suppose to be run");
        co_return 100;
    };
    SECTION("first task throws, rest tasks never run")
    {
        std::array<coral::task<int>, 3> tasks = {
            make_throwing_int_task("error"), never_run_task(), never_run_task()};

        REQUIRE_THROWS_AS(coral::sync_wait(coral::when_all(tasks)), std::runtime_error);
    }
    SECTION("second task throws, third never runs")
    {
        bool flag = false;
        std::array<coral::task<int>, 3> tasks = {
            run_task(flag), make_throwing_int_task("error"), never_run_task()};

        REQUIRE_THROWS_AS(coral::sync_wait(coral::when_all(tasks)), std::runtime_error);
        REQUIRE(flag);
    }
    SECTION("second task throws, rest tasks never run")
    {
        bool flag = false;
        std::array<coral::task<int>, 4> tasks = {
            run_task(flag), make_throwing_int_task("error"), never_run_task(), never_run_task()};

        REQUIRE_THROWS_AS(coral::sync_wait(coral::when_all(tasks)), std::runtime_error);
        REQUIRE(flag);
    }
    SECTION("last task throws")
    {
        bool flag = false;
        std::array<coral::task<int>, 2> tasks = {run_task(flag), make_throwing_int_task("error")};

        REQUIRE_THROWS_AS(coral::sync_wait(coral::when_all(tasks)), std::runtime_error);
        REQUIRE(flag);
    }
    SECTION("both task throws")
    {
        std::array<coral::task<int>, 2> tasks = {
            make_throwing_int_task("error1"), make_throwing_int_task("error2")};

        REQUIRE_THROWS_WITH(coral::sync_wait(coral::when_all(tasks)), "error1");
    }
}

TEST_CASE(
    "when_all range with stop_source - exception triggers stop for long-running tasks")
{
    std::stop_source stop_source;
    auto token = stop_source.get_token();

    std::vector<coral::task<int>> tasks;
    tasks.push_back(make_stoppable_task(token, 10, std::chrono::milliseconds(100)));
    tasks.push_back(make_throwing_int_task("error"));

    auto start = std::chrono::steady_clock::now();

    REQUIRE_THROWS_AS(coral::sync_wait(coral::when_all(stop_source, std::move(tasks))),
        std::runtime_error);

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    REQUIRE(stop_source.stop_requested());
    REQUIRE(duration < std::chrono::milliseconds(30));
}

#if CORAL_EXPECTED

TEST_CASE("when_any with expected")
{
    auto run_task = [](bool& flag) -> coral::task<std::expected<int, std::error_code>> {
        flag = true;
        co_return 100;
    };
    auto never_run_task = []() -> coral::task<std::expected<int, std::error_code>> {
        FAIL("This task is not suppose to be run");
        co_return 100;
    };
    auto throws_exception =
        [](std::string_view error) -> coral::task<std::expected<int, std::error_code>> {
        throw std::runtime_error(std::string(error));
        co_return 100;
    };
    SECTION("different types")
    {
        SECTION("int")
        {
            auto task = [] -> coral::task<std::expected<int, int>> {
                co_return std::expected<int, int>{};
            }();
            using expected_result_type = std::expected<std::tuple<int>, std::variant<int>>;

            [[maybe_unused]] auto result =
                coral::sync_wait(coral::when_all(std::move(task)));
            STATIC_REQUIRE(std::is_same_v<decltype(result), expected_result_type>);
        }
        SECTION("void")
        {
            auto task = [] -> coral::task<std::expected<void, int>> {
                co_return std::expected<void, int>{};
            }();
            using expected_result_type =
                std::expected<std::tuple<std::monostate>, std::variant<int>>;

            [[maybe_unused]] auto result =
                coral::sync_wait(coral::when_all(std::move(task)));
            STATIC_REQUIRE(std::is_same_v<decltype(result), expected_result_type>);
        }
    }
    SECTION("all values and errors are same")
    {
        using expected_result_type =
            std::expected<std::tuple<int, int, int>, std::variant<std::error_code>>;
        SECTION("all success")
        {
            auto result = coral::sync_wait(coral::when_all(
                make_int2error_code(10), make_int2error_code(20), make_int2error_code(30)));
            STATIC_REQUIRE(std::is_same_v<expected_result_type, decltype(result)>);
            REQUIRE(result.has_value());
            REQUIRE(result == std::make_tuple(10, 20, 30));
        }
        SECTION("first return error")
        {
            auto result = coral::sync_wait(coral::when_all(
                make_int2error_code(std::make_error_code(std::errc::wrong_protocol_type)),
                never_run_task(),
                never_run_task()));
            STATIC_REQUIRE(std::is_same_v<expected_result_type, decltype(result)>);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(std::get<0>(result.error()) == std::errc::wrong_protocol_type);
        }
        SECTION("first throws")
        {
            REQUIRE_THROWS_WITH(
                coral::sync_wait(coral::when_all(
                    throws_exception("error1"), never_run_task(), never_run_task())),
                "error1");
        }
        SECTION("second returns error")
        {
            bool flag = false;
            auto result = coral::sync_wait(coral::when_all(run_task(flag),
                make_int2error_code(std::make_error_code(std::errc::wrong_protocol_type)),
                never_run_task()));
            STATIC_REQUIRE(std::is_same_v<expected_result_type, decltype(result)>);
            REQUIRE(flag);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(std::get<0>(result.error()) == std::errc::wrong_protocol_type);
        }
        SECTION("second throws")
        {
            bool flag = false;
            REQUIRE_THROWS_WITH(coral::sync_wait(coral::when_all(
                                    run_task(flag), throws_exception("error1"), never_run_task())),
                "error1");
            REQUIRE(flag);
        }
        SECTION("first return error, second throws")
        {
            auto result = coral::sync_wait(coral::when_all(
                make_int2error_code(std::make_error_code(std::errc::wrong_protocol_type)),
                throws_exception("error1"),
                never_run_task()));
            STATIC_REQUIRE(std::is_same_v<expected_result_type, decltype(result)>);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(std::get<0>(result.error()) == std::errc::wrong_protocol_type);
        }
    }
    SECTION("values are different, errors are same")
    {
        using expected_result_type =
            std::expected<std::tuple<int, double>, std::variant<std::error_code>>;
        SECTION("all success")
        {
            auto result = coral::sync_wait(
                coral::when_all(make_int2error_code(10), make_double2error_code(20.)));
            STATIC_REQUIRE(std::is_same_v<expected_result_type, decltype(result)>);
            REQUIRE(result.has_value());
            REQUIRE(result == std::make_tuple(10, 20.));
        }
    }
    SECTION("values are same, errors are different")
    {
        using expected_result_type = std::expected<std::tuple<int, int>,
            std::variant<std::error_code, std::unique_ptr<int>>>;
        SECTION("all success")
        {
            auto result = coral::sync_wait(
                coral::when_all(make_int2error_code(10), make_int2unique_ptr(20)));
            STATIC_REQUIRE(std::is_same_v<expected_result_type, decltype(result)>);
            REQUIRE(result.has_value());
            REQUIRE(result == std::make_tuple(10, 20));
        }
        SECTION("first fails first")
        {
            auto result = coral::sync_wait(coral::when_all(
                make_int2error_code(std::make_error_code(std::errc::wrong_protocol_type), 5ms),
                make_int2unique_ptr(std::make_unique<int>(100), 20ms)));
            STATIC_REQUIRE(std::is_same_v<expected_result_type, decltype(result)>);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(std::holds_alternative<std::error_code>(result.error()));
            REQUIRE(std::get<std::error_code>(result.error()) == std::errc::wrong_protocol_type);
        }
        SECTION("second fails first")
        {
            auto result = coral::sync_wait(coral::when_all(
                make_int2error_code(std::make_error_code(std::errc::wrong_protocol_type), 20ms),
                make_int2unique_ptr(std::make_unique<int>(100), 5ms)));
            STATIC_REQUIRE(std::is_same_v<expected_result_type, decltype(result)>);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(std::holds_alternative<std::unique_ptr<int>>(result.error()));
            REQUIRE(std::get<std::unique_ptr<int>>(result.error()) != nullptr);
            REQUIRE(*std::get<std::unique_ptr<int>>(result.error()) == 100);
        }
    }
    SECTION("exceptions")
    {
        REQUIRE_THROWS_WITH(coral::sync_wait(coral::when_all(
                                make_int2error_code(10), make_throwing_void_task("error"))),
            "error");
    }
}

TEST_CASE("when_any range with expected")
{
    SECTION("different types")
    {
        SECTION("int")
        {
            auto task = [] -> coral::task<std::expected<int, int>> {
                co_return std::expected<int, int>{};
            }();
            using expected_result_type = std::expected<std::vector<int>, int>;

            std::vector<decltype(task)> tasks;
            tasks.push_back(std::move(task));

            [[maybe_unused]] auto result =
                coral::sync_wait(coral::when_all(std::move(tasks)));

            STATIC_REQUIRE(std::is_same_v<decltype(result), expected_result_type>);
        }
        SECTION("void")
        {
            auto task = [] -> coral::task<std::expected<void, int>> {
                co_return std::expected<void, int>{};
            }();
            using expected_result_type = std::expected<void, int>;

            std::vector<decltype(task)> tasks;
            tasks.push_back(std::move(task));

            [[maybe_unused]] auto result =
                coral::sync_wait(coral::when_all(std::move(tasks)));

            STATIC_REQUIRE(std::is_same_v<decltype(result), expected_result_type>);
        }
    }
    SECTION("all values and errors are same")
    {
        using expected_result_type = std::expected<std::vector<int>, std::error_code>;
        auto run_task = [](bool& flag) -> coral::task<std::expected<int, std::error_code>> {
            flag = true;
            co_return 100;
        };
        auto never_run_task = []() -> coral::task<std::expected<int, std::error_code>> {
            FAIL("This task is not suppose to be run");
            co_return 100;
        };
        SECTION("all success")
        {
            std::vector<coral::task<std::expected<int, std::error_code>>> tasks;
            tasks.push_back(make_int2error_code(10));
            tasks.push_back(make_int2error_code(20));

            auto result = coral::sync_wait(coral::when_all(std::move(tasks)));

            STATIC_REQUIRE(std::is_same_v<expected_result_type, decltype(result)>);

            REQUIRE(result.has_value());
            auto& v = result.value();
            REQUIRE(v.size() == 2);
            REQUIRE(v[0] == 10);
            REQUIRE(v[1] == 20);
        }
        SECTION("only fails")
        {
            std::vector<coral::task<std::expected<int, std::error_code>>> tasks;
            tasks.push_back(
                make_int2error_code(std::make_error_code(std::errc::wrong_protocol_type)));

            auto result = coral::sync_wait(coral::when_all(std::move(tasks)));

            STATIC_REQUIRE(std::is_same_v<expected_result_type, decltype(result)>);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == std::errc::wrong_protocol_type);
        }
        SECTION("first fails, others never run")
        {
            std::vector<coral::task<std::expected<int, std::error_code>>> tasks;
            tasks.push_back(
                make_int2error_code(std::make_error_code(std::errc::wrong_protocol_type)));
            tasks.push_back(never_run_task());
            tasks.push_back(never_run_task());

            auto result = coral::sync_wait(coral::when_all(std::move(tasks)));

            STATIC_REQUIRE(std::is_same_v<expected_result_type, decltype(result)>);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == std::errc::wrong_protocol_type);
        }
        SECTION("second fails, second never runs")
        {
            bool flag = false;
            std::array<coral::task<std::expected<int, std::error_code>>, 3> tasks = {
                run_task(flag),
                make_int2error_code(std::make_error_code(std::errc::wrong_protocol_type)),
                never_run_task(),
            };

            auto result = coral::sync_wait(coral::when_all(std::move(tasks)));

            STATIC_REQUIRE(std::is_same_v<expected_result_type, decltype(result)>);
            REQUIRE(flag);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == std::errc::wrong_protocol_type);
        }
        SECTION("second fails, left never run")
        {
            bool flag = false;
            std::array<coral::task<std::expected<int, std::error_code>>, 4> tasks = {
                run_task(flag),
                make_int2error_code(std::make_error_code(std::errc::wrong_protocol_type)),
                never_run_task(),
                never_run_task(),
            };

            auto result = coral::sync_wait(coral::when_all(std::move(tasks)));

            STATIC_REQUIRE(std::is_same_v<expected_result_type, decltype(result)>);
            REQUIRE(flag);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == std::errc::wrong_protocol_type);
        }
        SECTION("last fails")
        {
            bool flag = false;
            std::array<coral::task<std::expected<int, std::error_code>>, 2> tasks = {
                run_task(flag),
                make_int2error_code(std::make_error_code(std::errc::wrong_protocol_type)),
            };

            auto result = coral::sync_wait(coral::when_all(std::move(tasks)));

            STATIC_REQUIRE(std::is_same_v<expected_result_type, decltype(result)>);
            REQUIRE(flag);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == std::errc::wrong_protocol_type);
        }
        SECTION("both fail")
        {
            std::vector<coral::task<std::expected<int, std::error_code>>> tasks;
            tasks.push_back(
                make_int2error_code(std::make_error_code(std::errc::wrong_protocol_type)));
            tasks.push_back(make_int2error_code(std::make_error_code(std::errc::protocol_error)));

            auto result = coral::sync_wait(coral::when_all(std::move(tasks)));

            STATIC_REQUIRE(std::is_same_v<expected_result_type, decltype(result)>);
            REQUIRE_FALSE(result.has_value());
            REQUIRE(result.error() == std::errc::wrong_protocol_type);
        }
    }
    SECTION("exceptions")
    {
        auto task = [] -> coral::task<std::expected<void, int>> {
            throw std::runtime_error("error");
            co_return std::expected<void, int>{};
        }();
        std::vector<coral::task<std::expected<void, int>>> tasks;
        tasks.push_back(std::move(task));

        REQUIRE_THROWS_WITH(coral::sync_wait(coral::when_all(std::move(tasks))), "error");
    }
}
#endif