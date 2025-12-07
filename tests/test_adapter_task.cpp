#include <coral/detail/adapter_task.hpp>

#include "coral/task.hpp"

#include <catch2/catch_test_macros.hpp>

#include <coroutine>

TEST_CASE("inner awaitable destroys")
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

    SECTION("void")
    {
        auto t = [](coro_arg arg) -> coral::task<> { co_return; };
        bool flag = false;

        auto adapter_task = coral::detail::make_adapter_task(t(coro_arg{&flag}));

        auto callback = [&flag](bool success) -> std::coroutine_handle<> {
            REQUIRE(success);
            REQUIRE(flag == true);
            return std::noop_coroutine();
        };
        adapter_task.start(callback);
        REQUIRE(flag == true);
    }

    SECTION("int")
    {
        auto t = [](coro_arg arg) -> coral::task<int> { co_return 100; };
        bool flag = false;

        auto adapter_task = coral::detail::make_adapter_task(t(coro_arg{&flag}));

        auto callback = [&flag](bool success) -> std::coroutine_handle<> {
            REQUIRE(success);
            REQUIRE(flag == true);
            return std::noop_coroutine();
        };
        adapter_task.start(callback);
        REQUIRE(flag == true);
        REQUIRE(adapter_task.get_result_value() == 100);
    }
}