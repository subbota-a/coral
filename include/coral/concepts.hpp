#pragma once

#include <concepts>
#include <coroutine>
#include <utility>

namespace coral {

namespace detail {
template <typename T>
concept has_member_co_await = requires(T&& t) {
    { std::forward<T>(t).operator co_await() };
};

template <typename T>
concept has_free_co_await = requires(T&& t) {
    { operator co_await(std::forward<T>(t)) };
};

template <typename T>
concept valid_await_suspend_return = std::same_as<T, void> || std::same_as<T, bool> ||
    std::convertible_to<T, std::coroutine_handle<>>;
} // namespace detail

/// Concept for types that satisfy the awaiter interface
/// An awaiter must have await_ready(), await_suspend(), and await_resume() methods
template <typename T>
concept awaiter = requires(T&& t, std::coroutine_handle<> h) {
    { t.await_ready() } -> std::convertible_to<bool>;
    { t.await_suspend(h) } -> detail::valid_await_suspend_return;
    { t.await_resume() };
};

/// Concept for types that can be used with co_await
/// A type is awaitable if it's an awaiter, has operator co_await (member or free),
/// or can be converted to an awaiter
template <typename T>
concept awaitable = awaiter<T> || detail::has_member_co_await<T> || detail::has_free_co_await<T>;

template<typename T>
concept scheduler = requires(T& s, std::coroutine_handle<> h) {
    s.schedule(h);
};

} // namespace coral
