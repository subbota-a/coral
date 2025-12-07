#include <coral/sync_wait.hpp>
#include <coral/when_any.hpp>
#include <coral/when_stopped.hpp>

#include "coral/traits.hpp"
#include "helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <chrono>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <system_error>
#include <type_traits>

using namespace coral;
using namespace std::chrono_literals;

// =============================================================================
// Type support tests - verify compilation and correct type transformations
// =============================================================================

TEST_CASE("when_any variadic type support", "[when_any][types]")
{
    SECTION("void")
    {
        auto [index, value] = sync_wait(when_any(make_void_task(), make_void_task()));
        using expected = std::variant<std::monostate>;
        STATIC_REQUIRE(std::is_same_v<decltype(value), expected>);
        REQUIRE(index == 0);
    }
    SECTION("int")
    {
        auto [index, value] = sync_wait(when_any(make_int_task(42), make_int_task(43)));
        using expected = std::variant<int>;
        STATIC_REQUIRE(std::is_same_v<decltype(value), expected>);
        REQUIRE(index == 0);
        REQUIRE(std::get<int>(value) == 42);
    }
    SECTION("int&")
    {
        auto [index, value] = sync_wait(when_any(make_int_ref_task(), make_int_ref_task()));
        using expected = std::variant<std::reference_wrapper<int>>;
        STATIC_REQUIRE(std::is_same_v<decltype(value), expected>);
        REQUIRE(index == 0);
        REQUIRE(&std::get<0>(value).get() == &test_int_value);
    }
    SECTION("const int&")
    {
        auto [index, value] =
            sync_wait(when_any(make_const_int_ref_task(), make_const_int_ref_task()));
        using expected = std::variant<std::reference_wrapper<const int>>;
        STATIC_REQUIRE(std::is_same_v<decltype(value), expected>);
        REQUIRE(index == 0);
        REQUIRE(&std::get<0>(value).get() == &test_const_int_value);
    }
    SECTION("int*")
    {
        auto [index, value] = sync_wait(when_any(make_int_ptr_task(), make_int_ptr_task()));
        using expected = std::variant<int*>;
        STATIC_REQUIRE(std::is_same_v<decltype(value), expected>);
        REQUIRE(index == 0);
        REQUIRE(std::get<0>(value) == &test_int_value);
    }
    SECTION("const int*")
    {
        auto [index, value] =
            sync_wait(when_any(make_const_int_ptr_task(), make_const_int_ptr_task()));
        using expected = std::variant<const int*>;
        STATIC_REQUIRE(std::is_same_v<decltype(value), expected>);
        REQUIRE(index == 0);
        REQUIRE(std::get<0>(value) == &test_const_int_value);
    }
    SECTION("unique_ptr (move-only)")
    {
        auto [index, value] =
            sync_wait(when_any(make_unique_ptr_task(42), make_unique_ptr_task(43)));
        using expected = std::variant<std::unique_ptr<int>>;
        STATIC_REQUIRE(std::is_same_v<decltype(value), expected>);
        REQUIRE(index == 0);
        REQUIRE(*std::get<0>(value) == 42);
    }
    SECTION("mixed types")
    {
        auto [index, value] = sync_wait(when_any(make_int_task(42),
            make_void_task(),
            make_int_ref_task(),
            make_const_int_ref_task(),
            make_int_ptr_task(),
            make_const_int_ptr_task(),
            make_unique_ptr_task(42)));
        using expected = std::variant<int,
            std::monostate,
            std::reference_wrapper<int>,
            std::reference_wrapper<const int>,
            int*,
            const int*,
            std::unique_ptr<int>>;
        STATIC_REQUIRE(std::is_same_v<decltype(value), expected>);
    }
#if CORAL_EXPECTED
    SECTION("expected<int, error_code>")
    {
        auto result =
            sync_wait(when_any(make_int2error_code(42), make_int2error_code(43)));
        using expected = std::pair<size_t,
            std::expected<std::variant<int>, std::variant<std::error_code>>>;
        STATIC_REQUIRE(std::is_same_v<decltype(result), expected>);
        REQUIRE(result.first == 0);
        REQUIRE(result.second.has_value());
    }
    SECTION("expected<unique_ptr, int>")
    {
        auto result = sync_wait(when_any(
            make_unique_ptr2int(std::make_unique<int>(42)),
            make_unique_ptr2int(std::make_unique<int>(43))));
        using expected = std::pair<size_t,
            std::expected<std::variant<std::unique_ptr<int>>, std::variant<int>>>;
        STATIC_REQUIRE(std::is_same_v<decltype(result), expected>);
        REQUIRE(result.first == 0);
        REQUIRE(result.second.has_value());
    }
    SECTION("mixed types with expected")
    {
        auto result = sync_wait(when_any(make_int_task(42),
            make_void_task(),
            make_int_ref_task(),
            make_const_int_ref_task(),
            make_int_ptr_task(),
            make_const_int_ptr_task(),
            make_unique_ptr_task(42),
            make_int2unique_ptr(100),
            make_int2error_code(100)));
        using expected = std::pair<size_t,
            std::expected<std::variant<int,
                              std::monostate,
                              std::reference_wrapper<int>,
                              std::reference_wrapper<const int>,
                              int*,
                              const int*,
                              std::unique_ptr<int>>,
                std::variant<std::unique_ptr<int>, std::error_code>>>;
        STATIC_REQUIRE(std::is_same_v<decltype(result), expected>);
    }
#endif
}

TEST_CASE("when_any range type support", "[when_any][types][range]")
{
    SECTION("void")
    {
        std::vector<coral::task<void>> tasks;
        tasks.push_back(make_void_task());
        tasks.push_back(make_void_task());

        auto index = coral::sync_wait(coral::when_any(tasks));

        using actual = decltype(index);
        using expected = size_t;
        STATIC_REQUIRE(std::is_same_v<actual, expected>);
        REQUIRE(index == 0);
    }
    SECTION("int")
    {
        std::vector<coral::task<int>> tasks;
        tasks.push_back(make_int_task(10));
        tasks.push_back(make_int_task(20));

        auto result = coral::sync_wait(coral::when_any(tasks));

        using actual = decltype(result);
        using expected = std::pair<size_t, int>;
        STATIC_REQUIRE(std::is_same_v<actual, expected>);
        REQUIRE(result.first == 0);
        REQUIRE(result.second == 10);
    }
    SECTION("int&")
    {
        std::vector<coral::task<int&>> tasks;
        tasks.push_back(make_int_ref_task());
        tasks.push_back(make_int_ref_task());

        auto result = coral::sync_wait(coral::when_any(tasks));

        using actual = decltype(result);
        using expected = std::pair<size_t, int&>;
        STATIC_REQUIRE(std::is_same_v<actual, expected>);
        REQUIRE(result.first == 0);
        REQUIRE(&result.second == &test_int_value);
    }
    SECTION("const int&")
    {
        std::vector<coral::task<const int&>> tasks;
        tasks.push_back(make_const_int_ref_task());
        tasks.push_back(make_const_int_ref_task());

        auto result = coral::sync_wait(coral::when_any(tasks));

        using actual = decltype(result);
        using expected = std::pair<size_t, const int&>;
        STATIC_REQUIRE(std::is_same_v<actual, expected>);
        REQUIRE(result.first == 0);
        REQUIRE(&result.second == &test_const_int_value);
    }
    SECTION("int*")
    {
        std::vector<coral::task<int*>> tasks;
        tasks.push_back(make_int_ptr_task());
        tasks.push_back(make_int_ptr_task());

        auto result = coral::sync_wait(coral::when_any(tasks));

        using actual = decltype(result);
        using expected = std::pair<size_t, int*>;
        STATIC_REQUIRE(std::is_same_v<actual, expected>);
        REQUIRE(result.first == 0);
        REQUIRE(result.second == &test_int_value);
    }
    SECTION("const int*")
    {
        std::vector<coral::task<const int*>> tasks;
        tasks.push_back(make_const_int_ptr_task());
        tasks.push_back(make_const_int_ptr_task());

        auto result = coral::sync_wait(coral::when_any(tasks));

        using actual = decltype(result);
        using expected = std::pair<size_t, const int*>;
        STATIC_REQUIRE(std::is_same_v<actual, expected>);
        REQUIRE(result.first == 0);
        REQUIRE(result.second == &test_const_int_value);
    }
    SECTION("unique_ptr (move-only)")
    {
        std::vector<coral::task<std::unique_ptr<int>>> tasks;
        tasks.push_back(make_unique_ptr_task(10));
        tasks.push_back(make_unique_ptr_task(20));

        auto result = coral::sync_wait(coral::when_any(tasks));

        using actual = decltype(result);
        using expected = std::pair<size_t, std::unique_ptr<int>>;
        STATIC_REQUIRE(std::is_same_v<actual, expected>);
        REQUIRE(result.first == 0);
        REQUIRE(*result.second == 10);
    }
#if CORAL_EXPECTED
    SECTION("expected<void, unique_ptr<int>> with value")
    {
        using expected = std::expected<void, std::unique_ptr<int>>;
        std::vector<coral::task<expected>> tasks;
        tasks.push_back([]() -> coral::task<expected> { co_return expected{}; }());
        tasks.push_back([]() -> coral::task<expected> { co_return expected{}; }());

        auto result = coral::sync_wait(coral::when_any(tasks));

        using actual = decltype(result);
        using result_expected = std::pair<size_t, expected>;
        STATIC_REQUIRE(std::is_same_v<actual, result_expected>);
        REQUIRE(result.first == 0);
        REQUIRE(result.second.has_value());
    }
    SECTION("expected<void, unique_ptr<int>> with error")
    {
        using expected = std::expected<void, std::unique_ptr<int>>;
        std::vector<coral::task<expected>> tasks;
        tasks.push_back([]() -> coral::task<expected> {
            co_return std::unexpected(std::make_unique<int>(10));
        }());

        auto result = coral::sync_wait(coral::when_any(tasks));

        using actual = decltype(result);
        using result_expected = std::pair<size_t, expected>;
        STATIC_REQUIRE(std::is_same_v<actual, result_expected>);
        REQUIRE(result.first == 0);
        REQUIRE_FALSE(result.second.has_value());
        REQUIRE(*result.second.error() == 10);
    }
    SECTION("expected<unique_ptr<int>, int> with value")
    {
        using expected = std::expected<std::unique_ptr<int>, int>;
        std::vector<coral::task<expected>> tasks;
        tasks.push_back([]() -> coral::task<expected> { co_return std::make_unique<int>(10); }());
        tasks.push_back([]() -> coral::task<expected> { co_return std::make_unique<int>(20); }());

        auto result = coral::sync_wait(coral::when_any(tasks));

        using actual = decltype(result);
        using result_expected = std::pair<size_t, expected>;
        STATIC_REQUIRE(std::is_same_v<actual, result_expected>);
        REQUIRE(result.first == 0);
        REQUIRE(result.second.has_value());
        REQUIRE(*result.second.value() == 10);
    }
#endif
}

// =============================================================================
// First successful task returns - verify correct winner selection
// =============================================================================

TEST_CASE("when_any variadic first success returns", "[when_any][success]")
{
    auto never_run_task = []() -> coral::task<int> {
        FAIL("This task should not run");
        co_return 0;
    };

    SECTION("sync: single task succeeds")
    {
        auto [index, value] = sync_wait(when_any(make_int_task(42)));
        REQUIRE(index == 0);
        REQUIRE(std::get<int>(value) == 42);
    }
    SECTION("sync: first succeeds")
    {
        auto [index, value] =
            sync_wait(when_any(make_int_task(42), never_run_task(), never_run_task()));
        REQUIRE(index == 0);
        REQUIRE(std::get<int>(value) == 42);
    }
    SECTION("sync: middle succeeds, others fail")
    {
        auto [index, value] = sync_wait(when_any(
            make_throwing_int_task("error"), make_int_task(42), never_run_task()));
        REQUIRE(index == 1);
        REQUIRE(std::get<int>(value) == 42);
    }
    SECTION("sync: last succeeds, others fail")
    {
        auto [index, value] = sync_wait(
            when_any(make_throwing_int_task("error"), make_throwing_int_task("error"),
                make_int_task(42)));
        REQUIRE(index == 2);
        REQUIRE(std::get<int>(value) == 42);
    }
    SECTION("async: faster succeeds")
    {
        auto [index, value] = sync_wait(
            when_any(make_delayed_int_task(1, 25ms), make_delayed_int_task(42, 5ms)));
        REQUIRE(index == 1);
        REQUIRE(std::get<int>(value) == 42);
    }
    SECTION("async: faster throws, slower succeeds")
    {
        auto [index, value] = sync_wait(when_any(
            make_delayed_throwing_void_task("error", 10ms), make_delayed_int_task(42, 30ms)));
        REQUIRE(index == 1);
        REQUIRE(std::get<int>(value) == 42);
    }
    SECTION("async: faster succeeds, slower throws")
    {
        auto [index, value] = sync_wait(when_any(
            make_delayed_throwing_void_task("error", 30ms), make_delayed_int_task(42, 5ms)));
        REQUIRE(index == 1);
        REQUIRE(std::get<int>(value) == 42);
    }

#if CORAL_EXPECTED
    auto never_run_expected_task = []() -> coral::task<std::expected<int, std::error_code>> {
        FAIL("This task should not run");
        co_return 0;
    };
    auto error_code = std::make_error_code(std::errc::bad_message);

    SECTION("expected sync: first returns value")
    {
        auto [index, result] = sync_wait(when_any(
            make_int2error_code(42), never_run_expected_task(), never_run_expected_task()));
        REQUIRE(index == 0);
        REQUIRE(result.has_value());
    }
    SECTION("expected sync: first returns error, second returns value")
    {
        auto [index, result] = sync_wait(when_any(
            make_int2error_code(error_code), make_int2error_code(42), never_run_expected_task()));
        REQUIRE(index == 1);
        REQUIRE(result.has_value());
    }
    SECTION("expected async: faster returns value")
    {
        auto [index, result] = sync_wait(
            when_any(make_int2error_code(1, 25ms), make_int2error_code(42, 5ms)));
        REQUIRE(index == 1);
        REQUIRE(result.has_value());
    }
    SECTION("expected with unique_ptr value: first wins")
    {
        auto arg0 = std::make_unique<int>(10);
        auto* ptr0 = arg0.get();
        auto arg1 = std::make_unique<int>(20);

        auto [index, result] = sync_wait(when_any(
            make_unique_ptr2int(std::move(arg0)), make_unique_ptr2int(std::move(arg1), 5ms)));
        REQUIRE(index == 0);
        REQUIRE(result.has_value());
        REQUIRE(std::get<0>(result.value()).get() == ptr0);
    }
    SECTION("expected with unique_ptr value: second wins")
    {
        auto arg0 = std::make_unique<int>(10);
        auto arg1 = std::make_unique<int>(20);
        auto* ptr1 = arg1.get();

        auto [index, result] = sync_wait(when_any(
            make_unique_ptr2int(std::move(arg0), 5ms), make_unique_ptr2int(std::move(arg1))));
        REQUIRE(index == 1);
        REQUIRE(result.has_value());
        REQUIRE(std::get<0>(result.value()).get() == ptr1);
    }
#endif
}

TEST_CASE("when_any range first success returns", "[when_any][success][range]")
{
    auto never_run_task = []() -> coral::task<int> {
        FAIL("This task should not run");
        co_return 0;
    };

    SECTION("sync: single task succeeds")
    {
        std::vector<coral::task<int>> tasks;
        tasks.push_back(make_int_task(42));

        auto [index, value] = coral::sync_wait(coral::when_any(tasks));
        REQUIRE(index == 0);
        REQUIRE(value == 42);
    }
    SECTION("sync: first succeeds")
    {
        std::vector<coral::task<int>> tasks;
        tasks.push_back(make_int_task(42));
        tasks.push_back(never_run_task());
        tasks.push_back(never_run_task());

        auto [index, value] = coral::sync_wait(coral::when_any(tasks));
        REQUIRE(index == 0);
        REQUIRE(value == 42);
    }
    SECTION("sync: middle succeeds, others fail")
    {
        std::vector<coral::task<int>> tasks;
        tasks.push_back(make_throwing_int_task("error"));
        tasks.push_back(make_int_task(42));
        tasks.push_back(never_run_task());

        auto [index, value] = coral::sync_wait(coral::when_any(tasks));
        REQUIRE(index == 1);
        REQUIRE(value == 42);
    }
    SECTION("sync: last succeeds, others fail")
    {
        std::vector<coral::task<int>> tasks;
        tasks.push_back(make_throwing_int_task("error"));
        tasks.push_back(make_throwing_int_task("error"));
        tasks.push_back(make_int_task(42));

        auto [index, value] = coral::sync_wait(coral::when_any(tasks));
        REQUIRE(index == 2);
        REQUIRE(value == 42);
    }
    SECTION("async: faster succeeds")
    {
        std::vector<coral::task<int>> tasks;
        tasks.push_back(make_delayed_int_task(1, 25ms));
        tasks.push_back(make_delayed_int_task(42, 5ms));

        auto [index, value] = coral::sync_wait(coral::when_any(tasks));
        REQUIRE(index == 1);
        REQUIRE(value == 42);
    }

#if CORAL_EXPECTED
    auto never_run_expected_task = []() -> coral::task<std::expected<int, std::error_code>> {
        FAIL("This task should not run");
        co_return 0;
    };

    SECTION("expected sync: first returns value")
    {
        using expected = std::expected<int, std::error_code>;
        std::vector<coral::task<expected>> tasks;
        tasks.push_back(make_int2error_code(42));
        tasks.push_back(never_run_expected_task());

        auto [index, value] = coral::sync_wait(coral::when_any(tasks));
        REQUIRE(index == 0);
        REQUIRE(value.has_value());
        REQUIRE(value.value() == 42);
    }
    SECTION("expected sync: first returns error, second returns value")
    {
        using expected = std::expected<int, std::error_code>;
        std::vector<coral::task<expected>> tasks;
        tasks.push_back(make_int2error_code(std::make_error_code(std::errc::bad_message)));
        tasks.push_back(make_int2error_code(42));
        tasks.push_back(never_run_expected_task());

        auto [index, value] = coral::sync_wait(coral::when_any(tasks));
        REQUIRE(index == 1);
        REQUIRE(value.has_value());
        REQUIRE(value.value() == 42);
    }
    SECTION("expected async: faster returns value")
    {
        using expected = std::expected<int, std::error_code>;
        std::vector<coral::task<expected>> tasks;
        tasks.push_back(make_int2error_code(1, 25ms));
        tasks.push_back(make_int2error_code(42, 5ms));

        auto [index, value] = coral::sync_wait(coral::when_any(tasks));
        REQUIRE(index == 1);
        REQUIRE(value.has_value());
        REQUIRE(value.value() == 42);
    }
    SECTION("expected with unique_ptr value: first wins")
    {
        auto arg0 = std::make_unique<int>(10);
        auto* ptr0 = arg0.get();
        auto arg1 = std::make_unique<int>(20);

        using expected = std::expected<std::unique_ptr<int>, int>;
        std::vector<coral::task<expected>> tasks;
        tasks.push_back(make_unique_ptr2int(std::move(arg0)));
        tasks.push_back(make_unique_ptr2int(std::move(arg1), 5ms));

        auto [index, value] = coral::sync_wait(coral::when_any(tasks));
        REQUIRE(index == 0);
        REQUIRE(value.has_value());
        REQUIRE(value.value().get() == ptr0);
    }
    SECTION("expected with unique_ptr value: second wins")
    {
        auto arg0 = std::make_unique<int>(10);
        auto arg1 = std::make_unique<int>(20);
        auto* ptr1 = arg1.get();

        using expected = std::expected<std::unique_ptr<int>, int>;
        std::vector<coral::task<expected>> tasks;
        tasks.push_back(make_unique_ptr2int(std::move(arg0), 5ms));
        tasks.push_back(make_unique_ptr2int(std::move(arg1)));

        auto [index, value] = coral::sync_wait(coral::when_any(tasks));
        REQUIRE(index == 1);
        REQUIRE(value.has_value());
        REQUIRE(value.value().get() == ptr1);
    }
#endif
}

// =============================================================================
// All tasks fail - verify first error returned
// =============================================================================

TEST_CASE("when_any variadic all fail", "[when_any][fail]")
{
    SECTION("sync: single task throws")
    {
        REQUIRE_THROWS_WITH(sync_wait(when_any(make_throwing_int_task("Only"))), "Only");
    }
    SECTION("sync: all throw - first exception returned")
    {
        REQUIRE_THROWS_WITH(
            sync_wait(when_any(
                make_throwing_int_task("First"), make_throwing_int_task("Second"))),
            "First");
    }
    SECTION("async: all throw - fastest exception returned")
    {
        REQUIRE_THROWS_WITH(
            sync_wait(when_any(make_delayed_throwing_void_task("First", 30ms),
                make_delayed_throwing_void_task("Second", 1ms))),
            "Second");
    }

#if CORAL_EXPECTED
    SECTION("expected sync: all return error - first error returned")
    {
        auto [index, result] = sync_wait(when_any(
            make_int2error_code(std::make_error_code(std::errc::bad_message)),
            make_int2error_code(std::make_error_code(std::errc::io_error))));
        REQUIRE(index == 0);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(std::get<std::error_code>(result.error()) == std::errc::bad_message);
    }
    SECTION("expected async: all return error - fastest error returned")
    {
        auto [index, result] = sync_wait(when_any(
            make_int2error_code(std::make_error_code(std::errc::bad_message), 30ms),
            make_int2error_code(std::make_error_code(std::errc::io_error), 1ms)));
        REQUIRE(index == 1);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(std::get<std::error_code>(result.error()) == std::errc::io_error);
    }
    SECTION("expected with unique_ptr error: first fails first")
    {
        auto arg0 = std::make_unique<int>(10);
        auto* ptr0 = arg0.get();
        auto arg1 = std::make_unique<int>(20);

        auto [index, result] = sync_wait(when_any(
            make_int2unique_ptr(std::move(arg0)), make_int2unique_ptr(std::move(arg1), 5ms)));
        REQUIRE(index == 0);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(std::get<0>(result.error()).get() == ptr0);
    }
    SECTION("expected with unique_ptr error: second fails first")
    {
        auto arg0 = std::make_unique<int>(10);
        auto arg1 = std::make_unique<int>(20);
        auto* ptr1 = arg1.get();

        auto [index, result] = sync_wait(when_any(
            make_int2unique_ptr(std::move(arg0), 5ms), make_int2unique_ptr(std::move(arg1))));
        REQUIRE(index == 1);
        REQUIRE_FALSE(result.has_value());
        REQUIRE(std::get<0>(result.error()).get() == ptr1);
    }
    SECTION("expected: exception thrown - propagated")
    {
        using expected = std::expected<void, std::unique_ptr<int>>;
        std::vector<coral::task<expected>> tasks;
        tasks.push_back([]() -> coral::task<expected> {
            throw std::runtime_error("error");
            co_return expected{};
        }());

        REQUIRE_THROWS_WITH(coral::sync_wait(coral::when_any(tasks)), "error");
    }
#endif
}

TEST_CASE("when_any range all fail", "[when_any][fail][range]")
{
    SECTION("no tasks")
    {
        std::vector<coral::task<void>> void_tasks;
        REQUIRE_THROWS_WITH(
            coral::sync_wait(coral::when_any(std::move(void_tasks))), "no tasks");

        std::vector<coral::task<int>> int_tasks;
        REQUIRE_THROWS_WITH(
            coral::sync_wait(coral::when_any(std::move(int_tasks))), "no tasks");
    }
    SECTION("sync: single task throws")
    {
        std::vector<coral::task<int>> tasks;
        tasks.push_back(make_throwing_int_task("Only"));

        REQUIRE_THROWS_WITH(coral::sync_wait(coral::when_any(tasks)), "Only");
    }
    SECTION("sync: all throw - first exception returned")
    {
        std::vector<coral::task<int>> tasks;
        tasks.push_back(make_throwing_int_task("First"));
        tasks.push_back(make_throwing_int_task("Second"));

        REQUIRE_THROWS_WITH(coral::sync_wait(coral::when_any(tasks)), "First");
    }
    SECTION("async: all throw - fastest exception returned")
    {
        std::vector<coral::task<int>> tasks;
        tasks.push_back(make_delayed_throwing_int_task("First", 25ms));
        tasks.push_back(make_delayed_throwing_int_task("Second", 1ms));
        tasks.push_back(make_delayed_throwing_int_task("Third", 25ms));

        REQUIRE_THROWS_WITH(coral::sync_wait(coral::when_any(tasks)), "Second");
    }

#if CORAL_EXPECTED
    SECTION("expected no tasks")
    {
        using expected = std::expected<void, int>;
        std::vector<coral::task<expected>> tasks;
        REQUIRE_THROWS_WITH(
            coral::sync_wait(coral::when_any(std::move(tasks))), "no tasks");
    }
    SECTION("expected sync: all return error - first error returned")
    {
        using expected = std::expected<int, std::error_code>;
        std::vector<coral::task<expected>> tasks;
        tasks.push_back(make_int2error_code(std::make_error_code(std::errc::bad_message)));
        tasks.push_back(make_int2error_code(std::make_error_code(std::errc::io_error)));

        auto [index, value] = coral::sync_wait(coral::when_any(tasks));
        REQUIRE(index == 0);
        REQUIRE_FALSE(value.has_value());
        REQUIRE(value.error() == std::errc::bad_message);
    }
    SECTION("expected async: all return error - fastest error returned")
    {
        using expected = std::expected<int, std::error_code>;
        std::vector<coral::task<expected>> tasks;
        tasks.push_back(make_int2error_code(std::make_error_code(std::errc::bad_message), 50ms));
        tasks.push_back(make_int2error_code(std::make_error_code(std::errc::io_error), 1ms));
        tasks.push_back(make_int2error_code(std::make_error_code(std::errc::message_size), 50ms));

        auto [index, value] = coral::sync_wait(coral::when_any(tasks));
        REQUIRE(index == 1);
        REQUIRE_FALSE(value.has_value());
        REQUIRE(value.error() == std::errc::io_error);
    }
    SECTION("expected with unique_ptr error: first fails first")
    {
        auto arg0 = std::make_unique<int>(10);
        auto* ptr0 = arg0.get();
        auto arg1 = std::make_unique<int>(20);

        using expected = std::expected<int, std::unique_ptr<int>>;
        std::vector<coral::task<expected>> tasks;
        tasks.push_back(make_int2unique_ptr(std::move(arg0)));
        tasks.push_back(make_int2unique_ptr(std::move(arg1), 5ms));

        auto [index, value] = coral::sync_wait(coral::when_any(tasks));
        REQUIRE(index == 0);
        REQUIRE_FALSE(value.has_value());
        REQUIRE(value.error().get() == ptr0);
    }
    SECTION("expected with unique_ptr error: second fails first")
    {
        auto arg0 = std::make_unique<int>(10);
        auto arg1 = std::make_unique<int>(20);
        auto* ptr1 = arg1.get();

        using expected = std::expected<int, std::unique_ptr<int>>;
        std::vector<coral::task<expected>> tasks;
        tasks.push_back(make_int2unique_ptr(std::move(arg0), 5ms));
        tasks.push_back(make_int2unique_ptr(std::move(arg1)));

        auto [index, value] = coral::sync_wait(coral::when_any(tasks));
        REQUIRE(index == 1);
        REQUIRE_FALSE(value.has_value());
        REQUIRE(value.error().get() == ptr1);
    }
#endif
}

// =============================================================================
// Stop token cancellation
// =============================================================================

TEST_CASE("when_any stop_token cancellation", "[when_any][stop_token]")
{
    auto stoppable_int_task = [](std::stop_token token) -> coral::task<int> {
        co_await coral::when_stopped(token);
        co_return -1;
    };
    auto stoppable_void_task = [](std::stop_token token) -> coral::task<void> {
        co_await coral::when_stopped(token);
        co_return;
    };

    SECTION("variadic: sync success cancels others")
    {
        std::stop_source stop_source;

        auto [index, value] = sync_wait(when_any(stop_source,
            stoppable_int_task(stop_source.get_token()),
            make_int_task(42),
            stoppable_int_task(stop_source.get_token())));

        REQUIRE(index == 1);
        REQUIRE(std::get<int>(value) == 42);
        REQUIRE(stop_source.stop_requested());
    }
    SECTION("variadic: async success cancels others")
    {
        std::stop_source stop_source;

        auto [index, value] = sync_wait(when_any(stop_source,
            stoppable_void_task(stop_source.get_token()),
            async_delay(5ms),
            stoppable_void_task(stop_source.get_token())));

        REQUIRE(index == 1);
        REQUIRE(stop_source.stop_requested());
    }
    SECTION("range: sync success cancels others")
    {
        std::stop_source stop_source;

        std::vector<coral::task<int>> tasks;
        tasks.push_back(stoppable_int_task(stop_source.get_token()));
        tasks.push_back(make_int_task(42));
        tasks.push_back(stoppable_int_task(stop_source.get_token()));

        auto [index, value] = sync_wait(when_any(stop_source, tasks));

        REQUIRE(index == 1);
        REQUIRE(value == 42);
        REQUIRE(stop_source.stop_requested());
    }
    SECTION("range: async success cancels others")
    {
        std::stop_source stop_source;

        std::vector<coral::task<void>> tasks;
        tasks.push_back(stoppable_void_task(stop_source.get_token()));
        tasks.push_back(make_delayed_void_task(5ms));
        tasks.push_back(stoppable_void_task(stop_source.get_token()));

        auto index = sync_wait(when_any(stop_source, tasks));

        REQUIRE(index == 1);
        REQUIRE(stop_source.stop_requested());
    }

#if CORAL_EXPECTED
    auto stoppable_expected_task =
        [](std::stop_token token) -> coral::task<std::expected<int, std::error_code>> {
        co_await coral::when_stopped(token);
        co_return -1;
    };

    SECTION("variadic expected: sync success cancels others")
    {
        std::stop_source stop_source;

        auto [index, result] = sync_wait(when_any(stop_source,
            stoppable_expected_task(stop_source.get_token()),
            make_int2error_code(42),
            stoppable_expected_task(stop_source.get_token())));

        REQUIRE(index == 1);
        REQUIRE(result.has_value());
        REQUIRE(stop_source.stop_requested());
    }
    SECTION("variadic expected: async success cancels others")
    {
        std::stop_source stop_source;

        auto [index, result] = sync_wait(when_any(stop_source,
            stoppable_expected_task(stop_source.get_token()),
            make_int2error_code(42, 5ms),
            stoppable_expected_task(stop_source.get_token())));

        REQUIRE(index == 1);
        REQUIRE(result.has_value());
        REQUIRE(stop_source.stop_requested());
    }
    SECTION("range expected: sync success cancels others")
    {
        std::stop_source stop_source;

        using expected = std::expected<int, std::error_code>;
        std::vector<coral::task<expected>> tasks;
        tasks.push_back(stoppable_expected_task(stop_source.get_token()));
        tasks.push_back(make_int2error_code(42));
        tasks.push_back(stoppable_expected_task(stop_source.get_token()));

        auto [index, value] = sync_wait(when_any(stop_source, tasks));

        REQUIRE(index == 1);
        REQUIRE(value.has_value());
        REQUIRE(stop_source.stop_requested());
    }
    SECTION("range expected: async success cancels others")
    {
        std::stop_source stop_source;

        using expected = std::expected<int, std::error_code>;
        std::vector<coral::task<expected>> tasks;
        tasks.push_back(stoppable_expected_task(stop_source.get_token()));
        tasks.push_back(make_int2error_code(42, 5ms));
        tasks.push_back(stoppable_expected_task(stop_source.get_token()));

        auto [index, value] = sync_wait(when_any(stop_source, tasks));

        REQUIRE(index == 1);
        REQUIRE(value.has_value());
        REQUIRE(stop_source.stop_requested());
    }
#endif
}
