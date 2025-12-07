#include <coral/generator.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <vector>

// ============================================================================
// Basic Generator Tests
// ============================================================================

coral::generator<int> simple_generator()
{
    co_yield 1;
    co_yield 2;
    co_yield 3;
}

coral::generator<int> range_generator(int start, int end)
{
    for (int i = start; i < end; ++i) {
        co_yield i;
    }
}

coral::generator<int> empty_generator() { co_return; }

TEST_CASE("generator: basic iteration", "[generator]")
{
    auto gen = simple_generator();
    std::vector<int> result;

    for (int value : gen) {
        result.push_back(value);
    }

    REQUIRE(result == std::vector<int>{1, 2, 3});
}

TEST_CASE("generator: range-based for loop", "[generator]")
{
    std::vector<int> result;

    for (int value : range_generator(0, 5)) {
        result.push_back(value);
    }

    REQUIRE(result == std::vector<int>{0, 1, 2, 3, 4});
}

TEST_CASE("generator: empty generator", "[generator]")
{
    auto gen = empty_generator();
    std::vector<int> result;

    for (int value : gen) {
        result.push_back(value);
    }

    REQUIRE(result.empty());
}

TEST_CASE("generator: manual iteration", "[generator]")
{
    auto gen = simple_generator();
    auto it = gen.begin();

    REQUIRE(it != gen.end());
    REQUIRE(*it == 1);

    ++it;
    REQUIRE(it != gen.end());
    REQUIRE(*it == 2);

    ++it;
    REQUIRE(it != gen.end());
    REQUIRE(*it == 3);

    ++it;
    REQUIRE(it == gen.end());
}

// ============================================================================
// Reference Type Tests
// ============================================================================

coral::generator<const std::string&> string_ref_generator()
{
    static std::string a = "hello";
    static std::string b = "world";
    co_yield a;
    co_yield b;
}

TEST_CASE("generator: const reference types", "[generator]")
{
    std::vector<std::string> result;

    for (const std::string& s : string_ref_generator()) {
        result.push_back(s);
    }

    REQUIRE(result == std::vector<std::string>{"hello", "world"});
}

coral::generator<int&> mutable_ref_generator(std::vector<int>& vec)
{
    for (auto& v : vec) {
        co_yield v;
    }
}

TEST_CASE("generator: mutable references", "[generator]")
{
    std::vector<int> vec = {1, 2, 3};

    for (int& val : mutable_ref_generator(vec)) {
        val *= 2;
    }

    REQUIRE(vec == std::vector<int>{2, 4, 6});
}

// ============================================================================
// Move-Only Type Tests
// ============================================================================

coral::generator<std::unique_ptr<int>> unique_ptr_generator()
{
    co_yield std::make_unique<int>(1);
    co_yield std::make_unique<int>(2);
    co_yield std::make_unique<int>(3);
}

TEST_CASE("generator: move-only types", "[generator]")
{
    std::vector<int> values;

    for (auto&& ptr : unique_ptr_generator()) {
        values.push_back(*ptr);
    }

    REQUIRE(values == std::vector<int>{1, 2, 3});
}

// ============================================================================
// Exception Handling Tests
// ============================================================================

coral::generator<int> throwing_generator()
{
    co_yield 1;
    co_yield 2;
    throw std::runtime_error("generator error");
    co_yield 3; // Never reached
}

TEST_CASE("generator: exception propagation", "[generator]")
{
    auto gen = throwing_generator();
    auto it = gen.begin();

    REQUIRE(*it == 1);
    ++it;
    REQUIRE(*it == 2);

    REQUIRE_THROWS_AS(++it, std::runtime_error);
}

coral::generator<int> throw_on_first()
{
    throw std::runtime_error("immediate error");
    co_yield 1; // Never reached
}

TEST_CASE("generator: exception on first resume", "[generator]")
{
    auto gen = throw_on_first();
    REQUIRE_THROWS_AS(gen.begin(), std::runtime_error);
}

// ============================================================================
// Manual Nested Iteration (no elements_of)
// ============================================================================

coral::generator<int> inner_generator()
{
    co_yield 1;
    co_yield 2;
}

coral::generator<int> outer_generator()
{
    co_yield 0;
    // Manual iteration - no elements_of support yet
    for (int x : inner_generator()) {
        co_yield x;
    }
    co_yield 3;
}

TEST_CASE("generator: manual nested iteration", "[generator]")
{
    std::vector<int> result;

    for (int value : outer_generator()) {
        result.push_back(value);
    }

    REQUIRE(result == std::vector<int>{0, 1, 2, 3});
}

// ============================================================================
// Ranges Integration Tests
// ============================================================================

TEST_CASE("generator: is input_range", "[generator]")
{
    auto gen = range_generator(0, 10);
    static_assert(std::ranges::input_range<decltype(gen)>);
}

TEST_CASE("generator: works with std::ranges algorithms", "[generator]")
{
    auto gen = range_generator(0, 10);

    std::vector<int> result;
    std::ranges::copy(gen, std::back_inserter(result));

    REQUIRE(result == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9});
}

TEST_CASE("generator: works with views functional style", "[generator]")
{
    std::vector<int> result;
    for (int x : std::views::take(range_generator(0, 100), 5)) {
        result.push_back(x);
    }
    REQUIRE(result == std::vector<int>{0, 1, 2, 3, 4});

    result.clear();
    for (int x : std::views::transform(range_generator(1, 6), [](int x) { return x * x; })) {
        result.push_back(x);
    }
    REQUIRE(result == std::vector<int>{1, 4, 9, 16, 25});
}

TEST_CASE("generator: works with views pipeline style", "[generator]")
{
    std::vector<int> result;
    for (int x : range_generator(0, 100) | std::views::take(5)) {
        result.push_back(x);
    }
    REQUIRE(result == std::vector<int>{0, 1, 2, 3, 4});

    result.clear();
    for (int x : range_generator(1, 6) | std::views::transform([](int x) { return x * x; })) {
        result.push_back(x);
    }
    REQUIRE(result == std::vector<int>{1, 4, 9, 16, 25});

    result.clear();
    for (int x : range_generator(0, 20)
                 | std::views::transform([](int x) { return x * x; })
                 | std::views::filter([](int x) { return x % 2 == 0; })
                 | std::views::take(5)) {
        result.push_back(x);
    }
    REQUIRE(result == std::vector<int>{0, 4, 16, 36, 64});
}

// ============================================================================
// Move Semantics Tests
// ============================================================================

TEST_CASE("generator: move constructor", "[generator]")
{
    auto gen1 = simple_generator();
    auto it1 = gen1.begin();
    REQUIRE(*it1 == 1);

    auto gen2 = std::move(gen1);
    REQUIRE(++it1 != gen2.end());

    REQUIRE(*it1 == 2);
}

TEST_CASE("generator: move assignment", "[generator]")
{
    auto gen1 = simple_generator();
    auto gen2 = range_generator(10, 15);

    gen2 = std::move(gen1);

    std::vector<int> result;
    for (int value : gen2) {
        result.push_back(value);
    }

    REQUIRE(result == std::vector<int>{1, 2, 3});
}

// ============================================================================
// Edge Cases
// ============================================================================

coral::generator<int> single_element_generator()
{
    co_yield 42;
}

TEST_CASE("generator: single element", "[generator]")
{
    std::vector<int> result;

    for (int value : single_element_generator()) {
        result.push_back(value);
    }

    REQUIRE(result == std::vector<int>{42});
}

coral::generator<int> large_generator()
{
    for (int i = 0; i < 10000; ++i) {
        co_yield i;
    }
}

TEST_CASE("generator: large number of yields", "[generator]")
{
    int count = 0;
    int sum = 0;

    for (int value : large_generator()) {
        ++count;
        sum += value;
    }

    REQUIRE(count == 10000);
    REQUIRE(sum == 49995000); // Sum of 0..9999
}

// ============================================================================
// View Interface Tests
// ============================================================================

TEST_CASE("generator: bool conversion via view_interface", "[generator]")
{
    auto empty_gen = empty_generator();
    auto it = empty_gen.begin();
    REQUIRE(it == empty_gen.end());

    auto non_empty_gen = simple_generator();
    auto it2 = non_empty_gen.begin();
    REQUIRE(it2 != non_empty_gen.end());
}
