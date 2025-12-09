#pragma once

#include <coral/concepts.hpp>

#include <coroutine>
#include <exception>
#include <type_traits>
#include <utility>

namespace coral {

namespace detail {

// Type computations matching std::generator and coral::generator
template <typename Ref, typename V>
using async_generator_value_t = std::conditional_t<std::is_void_v<V>, std::remove_cvref_t<Ref>, V>;

template <typename Ref, typename V>
using async_generator_reference_t = std::conditional_t<std::is_void_v<V>, Ref&&, Ref>;

template <typename Reference>
using async_generator_yielded_t =
    std::conditional_t<std::is_reference_v<Reference>, Reference, const Reference&>;

} // namespace detail

template <typename Ref, typename V = void>
class async_generator;

namespace detail {

template <typename Ref, typename V>
class async_generator_promise {
public:
    using generator_type = async_generator<Ref, V>;
    using value_type = async_generator_value_t<Ref, V>;
    using reference = async_generator_reference_t<Ref, V>;
    using yielded = async_generator_yielded_t<reference>;
    using value_ptr = std::add_pointer_t<yielded>;

    async_generator_promise() noexcept = default;

    generator_type get_return_object() noexcept
    {
        return generator_type{std::coroutine_handle<async_generator_promise>::from_promise(*this)};
    }

    std::suspend_always initial_suspend() noexcept { return {}; }

    struct final_awaiter {
        bool await_ready() noexcept { return false; }

        std::coroutine_handle<> await_suspend(
            std::coroutine_handle<async_generator_promise> h) noexcept
        {
            auto& promise = h.promise();
            return promise.consumer_ ? promise.consumer_ : std::noop_coroutine();
        }

        void await_resume() noexcept {}
    };

    final_awaiter final_suspend() noexcept { return {}; }

    void unhandled_exception() noexcept { exception_ = std::current_exception(); }

    void return_void() noexcept {}

    // Awaiter returned by co_yield
    struct yield_awaiter {
        async_generator_promise& promise;

        bool await_ready() noexcept { return false; }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept
        {
            return promise.consumer_ ? promise.consumer_ : std::noop_coroutine();
        }

        void await_resume() noexcept {}
    };

    // Primary yield: accepts Yielded directly
    yield_awaiter yield_value(yielded val) noexcept
    {
        value_ = std::addressof(val);
        return yield_awaiter{*this};
    }

    // Copy awaiter for storing value in coroutine frame
    struct copy_awaiter {
        std::remove_cvref_t<yielded> copied;
        async_generator_promise* promise;

        bool await_ready() noexcept { return false; }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept
        {
            promise->value_ = std::addressof(copied);
            return promise->consumer_ ? promise->consumer_ : std::noop_coroutine();
        }

        void await_resume() noexcept {}
    };

    // Yield lvalue when yielded is rvalue reference: copy to coroutine frame
    copy_awaiter yield_value(std::remove_cvref_t<yielded> const& val)
        requires std::is_rvalue_reference_v<yielded>
        && std::copy_constructible<std::remove_cvref_t<yielded>>
    {
        return copy_awaiter{val, this};
    }

    void set_consumer(std::coroutine_handle<> consumer) noexcept { consumer_ = consumer; }

    value_ptr value() const noexcept { return value_; }

    void rethrow_if_exception()
    {
        if (exception_) {
            std::rethrow_exception(std::move(exception_));
        }
    }

private:
    value_ptr value_ = nullptr;
    std::exception_ptr exception_;
    std::coroutine_handle<> consumer_;
};

} // namespace detail

/// @brief Asynchronous generator coroutine that yields values lazily.
///
/// Unlike synchronous generator, async_generator supports co_await inside the coroutine body.
/// Use co_yield to produce values and co_await to perform asynchronous operations.
///
/// @tparam Ref Reference type (determines what co_yield accepts)
/// @tparam V Value type (defaults to remove_cvref_t<Ref>)
///
/// Example:
/// @code
/// async_generator<int> async_range(int n) {
///     for (int i = 0; i < n; ++i) {
///         co_await some_async_operation();
///         co_yield i;
///     }
/// }
///
/// coral::task<void> consumer() {
///     auto gen = async_range(5);
///     while (auto result = co_await gen.next()) {
///         process(*result);
///     }
/// }
/// @endcode
template <typename Ref, typename V>
class [[nodiscard]] async_generator {
public:
    using promise_type = detail::async_generator_promise<Ref, V>;
    using value_type = detail::async_generator_value_t<Ref, V>;
    using reference = detail::async_generator_reference_t<Ref, V>;

private:
    using handle_type = std::coroutine_handle<promise_type>;
    using yielded = detail::async_generator_yielded_t<reference>;

public:
    class next_awaiter;

    /// Lightweight wrapper over result, contains pointer to generator.
    /// nullptr means end of sequence.
    class result {
    public:
        explicit operator bool() const noexcept { return has_value(); }
        bool has_value() const noexcept { return gen_ != nullptr; }

        reference operator*() const noexcept { return value(); }

        reference value() const noexcept
        {
            return static_cast<reference>(*gen_->coro_.promise().value());
        }

    private:
        async_generator* gen_;

        friend class async_generator<Ref, V>::next_awaiter;
        result() noexcept
            : gen_(nullptr)
        {
        }
        explicit result(async_generator* gen) noexcept
            : gen_(gen)
        {
        }
    };

    /// Awaiter for next() operation
    class next_awaiter {
    public:
        explicit next_awaiter(async_generator& gen) noexcept
            : gen_(gen)
        {
        }

        bool await_ready() noexcept { return !gen_.coro_ || gen_.coro_.done(); }

        std::coroutine_handle<> await_suspend(std::coroutine_handle<> consumer) noexcept
        {
            gen_.coro_.promise().set_consumer(consumer);
            return gen_.coro_;
        }

        result await_resume()
        {
            if (!gen_.coro_ || gen_.coro_.done()) {
                gen_.coro_.promise().rethrow_if_exception();
                return result{};
            }
            gen_.coro_.promise().rethrow_if_exception();
            return result{&gen_};
        }

    private:
        async_generator& gen_;
    };

    async_generator() noexcept = default;

    async_generator(async_generator&& other) noexcept
        : coro_(std::exchange(other.coro_, nullptr))
    {
    }

    async_generator& operator=(async_generator&& other) noexcept
    {
        if (this != &other) {
            if (coro_) {
                coro_.destroy();
            }
            coro_ = std::exchange(other.coro_, nullptr);
        }
        return *this;
    }

    async_generator(const async_generator&) = delete;
    async_generator& operator=(const async_generator&) = delete;

    ~async_generator()
    {
        if (coro_) {
            coro_.destroy();
        }
    }

    /// Returns an awaitable that produces the next value or end-of-sequence marker.
    /// @return Awaiter that returns next_result on co_await
    next_awaiter next() noexcept { return next_awaiter{*this}; }

private:
    friend class detail::async_generator_promise<Ref, V>;

    explicit async_generator(handle_type coro) noexcept
        : coro_(coro)
    {
    }

    handle_type coro_{};
};

} // namespace coral
