#include <coral/generator.hpp>

#include <algorithm>
#include <functional>
#include <iostream>
#include <ranges>
#include <vector>

// Basic generator: yields values
coral::generator<int> iota(int n)
{
    for (int i = 0; i < n; ++i) {
        co_yield i;
    }
}

// Infinite sequence
coral::generator<int> fibonacci()
{
    int a = 0, b = 1;
    while (true) {
        co_yield a;
        auto next = a + b;
        a = b;
        b = next;
    }
}

// Yields mutable references - allows modification
coral::generator<int&> enumerate(std::vector<int>& vec)
{
    for (auto& elem : vec) {
        co_yield elem;
    }
}

int main()
{
    std::cout << "=== Generator Examples ===\n\n";

    // Basic usage with range-for
    std::cout << "iota(5): ";
    for (int x : iota(5)) {
        std::cout << x << " ";
    }
    std::cout << "\n\n";

    // Infinite sequence with break
    std::cout << "fibonacci (until > 100): ";
    for (int x : fibonacci()) {
        if (x > 100)
            break;
        std::cout << x << " ";
    }
    std::cout << "\n\n";

    // Using with range adaptors (pipeline syntax)
    std::cout << "First 5 even squares using adaptors: ";
    for (int x : iota(20)
                 | std::views::transform([](int x) { return x * x; })
                 | std::views::filter([](int x) { return x % 2 == 0; })
                 | std::views::take(5)) {
        std::cout << x << " ";
    }
    std::cout << "\n\n";

    // Mutable references
    std::cout << "Modifying vector through generator<int&>:\n";
    std::vector<int> vec{1, 2, 3, 4, 5};
    std::cout << "  Before: ";
    for (int x : vec) {
        std::cout << x << " ";
    }
    std::cout << "\n";

    for (int& x : enumerate(vec)) {
        x *= 2;
    }

    std::cout << "  After:  ";
    for (int x : vec) {
        std::cout << x << " ";
    }
    std::cout << "\n\n";

    // Using with algorithms (C++23 fold_left works with sentinel)
    std::cout << "Sum of iota(10) using std::ranges::fold_left: ";
    int sum = std::ranges::fold_left(iota(10), 0, std::plus<>{});
    std::cout << sum << "\n\n";

    std::cout << "=== Generator Examples Completed ===\n";
    return 0;
}
