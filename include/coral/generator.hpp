#pragma once

#include <coroutine>
#include <exception>
#include <iterator>
#include <ranges>
#include <type_traits>
#include <utility>

namespace coral {

namespace detail {

// Type computations matching std::generator
template <typename Ref, typename V>
using generator_value_t = std::conditional_t<std::is_void_v<V>, std::remove_cvref_t<Ref>, V>;

template <typename Ref, typename V>
using generator_reference_t = std::conditional_t<std::is_void_v<V>, Ref&&, Ref>;

template <typename Reference>
using generator_yielded_t =
    std::conditional_t<std::is_reference_v<Reference>, Reference, const Reference&>;

/// Base promise type for generator
template <typename Yielded>
class generator_promise {
    static_assert(std::is_reference_v<Yielded>);

public:
    using value_ptr = std::add_pointer_t<Yielded>;

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct final_awaiter {
        bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<>) noexcept {}
        void await_resume() noexcept {}
    };

    final_awaiter final_suspend() noexcept { return {}; }

    void unhandled_exception() { exception_ = std::current_exception(); }

    void return_void() noexcept {}

    // Primary yield: accepts Yielded directly
    // For generator<int&>: Yielded = int&, accepts lvalue ref
    // For generator<int>: Yielded = int&&, accepts rvalue ref
    std::suspend_always yield_value(Yielded val) noexcept
    {
        value_ = std::addressof(val);
        return {};
    }

    // Copy awaiter for storing value in coroutine frame
    struct copy_awaiter {
        std::remove_cvref_t<Yielded> copied;
        generator_promise* promise;

        bool await_ready() noexcept { return false; }

        void await_suspend(std::coroutine_handle<>) noexcept
        {
            promise->value_ = std::addressof(copied);
        }

        void await_resume() noexcept {}
    };

    // Yield lvalue when Yielded is rvalue reference: copy to coroutine frame
    // Example: generator<int> with co_yield some_int_lvalue
    // The const& overload catches lvalues that can't bind to int&&
    copy_awaiter yield_value(std::remove_cvref_t<Yielded> const& val)
        requires std::is_rvalue_reference_v<Yielded>
        && std::copy_constructible<std::remove_cvref_t<Yielded>>
    {
        return copy_awaiter{val, this};
    }

    void rethrow_if_exception()
    {
        if (exception_) {
            std::rethrow_exception(std::move(exception_));
        }
    }

    value_ptr value() const noexcept { return value_; }

private:
    value_ptr value_ = nullptr;
    std::exception_ptr exception_;
};

} // namespace detail

/// @brief Synchronous generator coroutine that yields values lazily.
///
/// Models std::ranges::input_range. Use co_yield to produce values.
///
/// @tparam Ref Reference type (determines what co_yield accepts)
/// @tparam V Value type (defaults to remove_cvref_t<Ref>)
///
/// Example:
/// @code
/// generator<int> iota(int n) {
///     for (int i = 0; i < n; ++i)
///         co_yield i;
/// }
///
/// for (int x : iota(5)) { ... }  // 0, 1, 2, 3, 4
/// @endcode

template <typename Ref, typename V = void>
class [[nodiscard]] generator : public std::ranges::view_interface<generator<Ref, V>> {
public:
    using value_type = detail::generator_value_t<Ref, V>;
    using reference = detail::generator_reference_t<Ref, V>;
    using yielded = detail::generator_yielded_t<reference>;

private:
    static_assert(std::is_reference_v<reference>
            || (std::is_object_v<reference> && std::copy_constructible<reference>),
        "reference must be a reference type or a copy_constructible object type");

    using promise_base = detail::generator_promise<yielded>;

public:
    class promise_type : public promise_base {
    public:
        generator get_return_object() noexcept
        {
            return generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
    };

private:
    using handle_type = std::coroutine_handle<promise_type>;

public:
    class iterator {
    public:
        using iterator_concept = std::input_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = generator::value_type;

        iterator(iterator&& other) noexcept
            : coro_(std::exchange(other.coro_, nullptr))
        {
        }

        iterator& operator=(iterator&& other) noexcept
        {
            coro_ = std::exchange(other.coro_, nullptr);
            return *this;
        }

        iterator(const iterator&) = delete;
        iterator& operator=(const iterator&) = delete;

        ~iterator() = default;

        friend bool operator==(const iterator& it, std::default_sentinel_t) noexcept
        {
            return !it.coro_ || it.coro_.done();
        }

        iterator& operator++()
        {
            coro_.resume();
            coro_.promise().rethrow_if_exception();
            return *this;
        }

        void operator++(int) { ++*this; }

        reference operator*() const noexcept
        {
            return static_cast<reference>(*coro_.promise().value());
        }

    private:
        friend class generator;

        explicit iterator(handle_type coro) noexcept
            : coro_(coro)
        {
        }

        handle_type coro_;
    };

    generator() noexcept = default;

    generator(generator&& other) noexcept
        : coro_(std::exchange(other.coro_, nullptr))
    {
    }

    generator& operator=(generator&& other) noexcept
    {
        if (this != &other) {
            if (coro_) {
                coro_.destroy();
            }
            coro_ = std::exchange(other.coro_, nullptr);
        }
        return *this;
    }

    generator(const generator&) = delete;
    generator& operator=(const generator&) = delete;

    ~generator()
    {
        if (coro_) {
            coro_.destroy();
        }
    }

    iterator begin()
    {
        if (coro_) {
            coro_.resume();
            coro_.promise().rethrow_if_exception();
        }
        return iterator{coro_};
    }

    constexpr std::default_sentinel_t end() const noexcept { return {}; }

private:
    explicit generator(handle_type coro) noexcept
        : coro_(coro)
    {
    }

    handle_type coro_{};
};

} // namespace coral
