#include <coral/detail/traits.hpp>
#include <coral/task.hpp>
#include <coral/traits.hpp>

#include "coral/detail/config.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <type_traits>
#include <variant>

// ============================================================================
// Test ResultOf type trait
// ============================================================================

TEST_CASE("ResultOf extracts type from Task<int>", "[concepts][ResultOf]")
{
    using result = coral::result_of<coral::task<int>>;
    STATIC_REQUIRE(std::is_same_v<result, int>);
}

TEST_CASE("ResultOf extracts void from Task<void>", "[concepts][ResultOf]")
{
    using result = coral::result_of<coral::task<void>>;
    STATIC_REQUIRE(std::is_same_v<result, void>);
}

TEST_CASE("ResultOf extracts reference from Task<int&>", "[concepts][ResultOf]")
{
    using result = coral::result_of<coral::task<int&>>;
    STATIC_REQUIRE(std::is_same_v<result, int&>);
}

TEST_CASE("ResultOf extracts const reference from Task<const int&>", "[concepts][ResultOf]")
{
    using result = coral::result_of<coral::task<const int&>>;
    STATIC_REQUIRE(std::is_same_v<result, const int&>);
}

TEST_CASE("ResultOf extracts string from Task<std::string>", "[concepts][ResultOf]")
{
    using result = coral::result_of<coral::task<std::string>>;
    STATIC_REQUIRE(std::is_same_v<result, std::string>);
}

TEST_CASE("ResultOf extracts string& from Task<std::string&>", "[concepts][ResultOf]")
{
    using result = coral::result_of<coral::task<std::string&>>;
    STATIC_REQUIRE(std::is_same_v<result, std::string&>);
}

TEST_CASE("ResultOf works with Task<unique_ptr>", "[concepts][ResultOf]")
{
    using result = coral::result_of<coral::task<std::unique_ptr<int>>>;
    STATIC_REQUIRE(std::is_same_v<result, std::unique_ptr<int>>);
}

// ============================================================================
// Test AwaiterOf type trait
// ============================================================================

TEST_CASE("AwaiterOf extracts awaiter from Task<int>", "[concepts][AwaiterOf]")
{
    using awaiter = coral::awaiter_of<coral::task<int>>;
    // The awaiter should satisfy the awaiter concept
    STATIC_REQUIRE(coral::awaiter<awaiter>);
}

TEST_CASE("AwaiterOf extracts awaiter from Task<void>", "[concepts][AwaiterOf]")
{
    using awaiter = coral::awaiter_of<coral::task<void>>;
    STATIC_REQUIRE(coral::awaiter<awaiter>);
}

TEST_CASE("AwaiterOf extracts awaiter from Task<std::string&>", "[concepts][AwaiterOf]")
{
    using awaiter = coral::awaiter_of<coral::task<std::string&>>;
    STATIC_REQUIRE(coral::awaiter<awaiter>);
}

// ============================================================================
// Test custom awaitable
// ============================================================================

// Custom awaiter that returns int
struct int_awaiter {
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    int await_resume() const noexcept { return 42; }
};

// Custom awaitable with member operator co_await
struct custom_awaitable {
    int_awaiter operator co_await() const noexcept { return int_awaiter{}; }
};

TEST_CASE("ResultOf works with custom awaitable", "[concepts][ResultOf][custom]")
{
    using result = coral::result_of<custom_awaitable>;
    STATIC_REQUIRE(std::is_same_v<result, int>);
}

TEST_CASE("AwaiterOf extracts awaiter from custom awaitable", "[concepts][AwaiterOf][custom]")
{
    using awaiter = coral::awaiter_of<custom_awaitable>;
    STATIC_REQUIRE(std::is_same_v<awaiter, int_awaiter>);
}

TEST_CASE("ResultOf works with direct awaiter", "[concepts][ResultOf][awaiter]")
{
    using result = coral::result_of<int_awaiter>;
    STATIC_REQUIRE(std::is_same_v<result, int>);
}

TEST_CASE("AwaiterOf returns same type for direct awaiter", "[concepts][AwaiterOf][awaiter]")
{
    using awaiter = coral::awaiter_of<int_awaiter>;
    STATIC_REQUIRE(std::is_same_v<awaiter, int_awaiter>);
}

// ============================================================================
// Test awaitable concept
// ============================================================================

TEST_CASE("Task<int> satisfies awaitable concept", "[concepts][awaitable]")
{
    STATIC_REQUIRE(coral::awaitable<coral::task<int>>);
}

TEST_CASE("Task<void> satisfies awaitable concept", "[concepts][awaitable]")
{
    STATIC_REQUIRE(coral::awaitable<coral::task<void>>);
}

TEST_CASE("Task<int&> satisfies awaitable concept", "[concepts][awaitable]")
{
    STATIC_REQUIRE(coral::awaitable<coral::task<int&>>);
}

TEST_CASE("CustomAwaitable satisfies awaitable concept", "[concepts][awaitable]")
{
    STATIC_REQUIRE(coral::awaitable<custom_awaitable>);
}

TEST_CASE("int_awaiter satisfies awaitable concept", "[concepts][awaitable]")
{
    STATIC_REQUIRE(coral::awaitable<int_awaiter>);
}

TEST_CASE("int does not satisfy awaitable concept", "[concepts][awaitable]")
{
    STATIC_REQUIRE(!coral::awaitable<int>);
}

TEST_CASE("std::string does not satisfy awaitable concept", "[concepts][awaitable]")
{
    STATIC_REQUIRE(!coral::awaitable<std::string>);
}

// ============================================================================
// Test awaiter concept
// ============================================================================

TEST_CASE("int_awaiter satisfies awaiter concept", "[concepts][awaiter]")
{
    STATIC_REQUIRE(coral::awaiter<int_awaiter>);
}

TEST_CASE("Task<int> does not satisfy awaiter concept directly", "[concepts][awaiter]")
{
    STATIC_REQUIRE(!coral::awaiter<coral::task<int>>);
}

TEST_CASE("int does not satisfy awaiter concept", "[concepts][awaiter]")
{
    STATIC_REQUIRE(!coral::awaiter<int>);
}

TEST_CASE("unique_variant with all different types", "[metaprogramming][unique_variant]")
{
    using result = coral::detail::unique_variant<int, int&, const int&, int*, const int*, void>;
    using expected = std::variant<int,
        std::reference_wrapper<int>,
        std::reference_wrapper<const int>,
        int*,
        const int*,
        std::monostate>;
    STATIC_REQUIRE(std::is_same_v<result, expected>);
}

TEST_CASE("unique_variant with duplicate types", "[metaprogramming][unique_variant]")
{
    using result = coral::detail::unique_variant<int, float, void, int, float, void>;
    using expected = std::variant<int, float, std::monostate>;
    STATIC_REQUIRE(std::is_same_v<result, expected>);
}

TEST_CASE("non_void_unique_variant with all different types",
    "[metaprogramming][non_void_unique_variant]")
{
    using result =
        coral::detail::non_void_unique_variant<int, int&, const int&, int*, const int*, void>;
    using expected = std::variant<int,
        std::reference_wrapper<int>,
        std::reference_wrapper<const int>,
        int*,
        const int*>;
    STATIC_REQUIRE(std::is_same_v<result, expected>);
}

TEST_CASE("non_void_unique_variant with duplicate types",
    "[metaprogramming][non_void_unique_variant]")
{
    using result = coral::detail::non_void_unique_variant<int, float, void, int, float, void>;
    using expected = std::variant<int, float>;
    STATIC_REQUIRE(std::is_same_v<result, expected>);
}

#if CORAL_EXPECTED

TEST_CASE("has_any_expected_in_tuple", "[metaprogramming][expected]")
{
    STATIC_REQUIRE_FALSE(
        coral::detail::has_any_expected_in_tuple_v<std::tuple<int, int&, const int&, int*>>);
    STATIC_REQUIRE(coral::detail::has_any_expected_in_tuple_v<
        std::tuple<int, int&, const int&, int*, std::expected<int, int>>>);
}

TEST_CASE("has_any_expected", "[metaprogramming][expected]")
{
    STATIC_REQUIRE_FALSE(coral::detail::has_any_expected_v<int, int&, const int&, int*, void>);
    STATIC_REQUIRE_FALSE(coral::detail::has_any_expected_v<void, void, void>);
    STATIC_REQUIRE(coral::detail::
            has_any_expected_v<void, int, int&, const int&, int*, std::expected<int, int>>);
}

TEST_CASE("tuple_of_values_t without expected", "[metaprogramming][expected]")
{
    using tuple = std::tuple<int, int&, const int&, int*, std::monostate>;
    using actual = coral::detail::tuple_of_values_t<tuple>;
    STATIC_REQUIRE(std::is_same_v<actual, tuple>);
}

TEST_CASE("tuple_of_values_t with expected", "[metaprogramming][expected]")
{
    using source = std::
        tuple<int, std::expected<float, size_t>, std::expected<double, size_t>, std::monostate>;
    using expected = std::tuple<int, float, double, std::monostate>;
    using actual = coral::detail::tuple_of_values_t<source>;
    STATIC_REQUIRE(std::is_same_v<actual, expected>);
}

TEST_CASE("tuple_of_errors_t", "[metaprogramming][expected]")
{
    using source = std::tuple<int, std::expected<float, size_t>, std::expected<double, float>>;
    using expected = std::variant<size_t, float>;
    using actual = coral::detail::variant_of_errors_t<source>;
    STATIC_REQUIRE(std::is_same_v<actual, expected>);
}

TEST_CASE("extract_value", "[metaprogramming][expected]")
{
    auto foo = []() -> std::unique_ptr<int> { return nullptr; };

    auto m = coral::detail::extract_value(foo());
    SUCCEED("extract_value forward rvalue");
}

TEST_CASE("extract_value_for_tuple expected", "[metaprogramming][expected]")
{
    std::expected<int, float> v{123};
    auto expected = v.value();
    SECTION("value")
    {
        auto r = coral::detail::for_tuple(coral::detail::extract_value(v));
        STATIC_REQUIRE(std::is_same_v<int, decltype(r)>);
        REQUIRE(r == expected);
    }
    SECTION("lvalue")
    {
        auto& s = v;
        auto r = coral::detail::for_tuple(coral::detail::extract_value(s));
        STATIC_REQUIRE(std::is_same_v<int, decltype(r)>);
        REQUIRE(r == expected);
    }
    SECTION("rvalue")
    {
        auto r = coral::detail::for_tuple(coral::detail::extract_value(std::move(v)));
        STATIC_REQUIRE(std::is_same_v<int, decltype(r)>);
        REQUIRE(r == expected);
    }
}

TEST_CASE("extract_values_for_tuple int", "[metaprogramming][expected]")
{
    int i = 121;
    using source_type = std::tuple<int>;
    auto source = source_type{i};
    auto actual = coral::detail::extract_values_for_tuple(source);
    using expected = std::tuple<int>;
    STATIC_REQUIRE(std::is_same_v<decltype(actual), expected>);
    auto [i_ref] = actual;
    REQUIRE(i_ref == i);
}

TEST_CASE("extract_values_for_tuple int&", "[metaprogramming][expected]")
{
    int i = 121;
    using source_type = std::tuple<int&>;
    auto source = source_type{i};
    auto actual = coral::detail::extract_values_for_tuple(source);
    using expected = std::tuple<int&>;
    STATIC_REQUIRE(std::is_same_v<decltype(actual), expected>);
    auto [i_ref] = actual;
    REQUIRE(&i_ref == &i);
}

TEST_CASE("extract_values_for_tuple expected", "[metaprogramming][expected]")
{
    std::expected<float, size_t> f = 348.1f;
    using source_type = std::tuple<std::expected<float, size_t>>;
    auto source = source_type{f};
    auto actual = coral::detail::extract_values_for_tuple(source);
    using expected = std::tuple<float>;
    STATIC_REQUIRE(std::is_same_v<decltype(actual), expected>);
    auto [f_copy] = actual;
    REQUIRE(f_copy == f.value());
}

TEST_CASE("extract_values_for_tuple mixin", "[metaprogramming][expected]")
{
    int i = 121;
    std::expected<float, size_t> f = 348.1f;
    using source_type =
        std::tuple<int, int&, std::expected<float, size_t>, std::expected<float, size_t>>;
    auto source = source_type{i, i, f, f};
    auto actual = coral::detail::extract_values_for_tuple(source);
    using expected = std::tuple<int, int&, float, float>;
    STATIC_REQUIRE(std::is_same_v<decltype(actual), expected>);
    auto [i_copy, i_ref, f_copy_1, f_copy_2] = actual;
    REQUIRE(i_copy == i);
    REQUIRE(&i_ref == &i);
    REQUIRE(f_copy_1 == f.value());
    REQUIRE(f_copy_2 == f.value());
}

TEST_CASE("extract_error_at_index", "[metaprogramming][expected]")
{
    int i = 121;
    std::expected<float, size_t> f1{std::unexpect, 1};
    std::expected<float, size_t> f2{std::unexpect, 2};
    std::expected<float, double> f3{std::unexpect, 3.0};
    using source_type = std::tuple<int,
        std::expected<float, size_t>,
        std::expected<float, size_t>,
        std::expected<float, double>>;
    auto source = source_type{i, f1, f2, f3};
    using expected_type = std::variant<size_t, double>;
    SECTION("value")
    {
        auto actual = coral::detail::extract_error_at_index<expected_type, 1>(source);
        STATIC_REQUIRE(std::is_same_v<decltype(actual), expected_type>);
        REQUIRE(std::holds_alternative<size_t>(actual));
        REQUIRE(std::get<size_t>(actual) == f1.error());
    }
    SECTION("lvalue")
    {
        auto& ref_source = source;
        auto actual = coral::detail::extract_error_at_index<expected_type, 1>(ref_source);
        STATIC_REQUIRE(std::is_same_v<decltype(actual), expected_type>);
        REQUIRE(std::holds_alternative<size_t>(actual));
        REQUIRE(std::get<size_t>(actual) == f1.error());
    }
    SECTION("rvalue")
    {
        auto actual = coral::detail::extract_error_at_index<expected_type, 1>(std::move(source));
        STATIC_REQUIRE(std::is_same_v<decltype(actual), expected_type>);
        REQUIRE(std::holds_alternative<size_t>(actual));
        REQUIRE(std::get<size_t>(actual) == f1.error());
    }
}

TEST_CASE("make_expected_of_error_at_index", "[metaprogramming][expected]")
{
    int i = 121;
    std::expected<float, size_t> f1{std::unexpect, 1};
    std::expected<float, size_t> f2{std::unexpect, 2};
    std::expected<float, double> f3{std::unexpect, 3.0};
    using source_type = std::tuple<int,
        std::expected<float, size_t>,
        std::expected<float, size_t>,
        std::expected<float, double>>;
    auto source = source_type{i, f1, f2, f3};
    using expected_type =
        std::expected<std::tuple<int, float, float, float>, std::variant<size_t, double>>;
    SECTION("value")
    {
        auto actual = coral::detail::make_expected_of_error_at_index(1, source);
        STATIC_REQUIRE(std::is_same_v<decltype(actual), expected_type>);
        REQUIRE_FALSE(actual.has_value());
        REQUIRE(std::holds_alternative<size_t>(actual.error()));
        REQUIRE(std::get<size_t>(actual.error()) == f1.error());
    }
    SECTION("lvalue")
    {
        auto& ref_source = source;
        auto actual = coral::detail::make_expected_of_error_at_index(1, ref_source);
        STATIC_REQUIRE(std::is_same_v<decltype(actual), expected_type>);
        REQUIRE_FALSE(actual.has_value());
        REQUIRE(std::holds_alternative<size_t>(actual.error()));
        REQUIRE(std::get<size_t>(actual.error()) == f1.error());
    }
    SECTION("rvalue")
    {
        auto actual = coral::detail::make_expected_of_error_at_index(1, std::move(source));
        STATIC_REQUIRE(std::is_same_v<decltype(actual), expected_type>);
        REQUIRE_FALSE(actual.has_value());
        REQUIRE(std::holds_alternative<size_t>(actual.error()));
        REQUIRE(std::get<size_t>(actual.error()) == f1.error());
    }
}
#endif