#pragma once
#include <coral/concepts.hpp>

namespace coral {

template <awaitable Awaitable>
awaiter auto create_awaiter(Awaitable&& awaitable) {
    if constexpr (detail::has_member_co_await<Awaitable>) {
        return std::forward<Awaitable>(awaitable).operator co_await();
    }
    if constexpr (detail::has_free_co_await<Awaitable>) {
        return operator co_await(std::forward<Awaitable>(awaitable));
    }
    if constexpr (awaiter<Awaitable>) {
        return std::forward<Awaitable>(awaitable);
    }
}

template <awaitable Awaitable>
using awaiter_of = decltype(create_awaiter(std::declval<Awaitable>()));

template <awaitable Awaitable>
using result_of = decltype(create_awaiter(std::declval<Awaitable>()).await_resume());

} // namespace coral
