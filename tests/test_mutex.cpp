#include <coral/mutex.hpp>
#include <coral/sync_wait.hpp>
#include <coral/when_all.hpp>

#include "helpers.hpp"
#include "scheduler.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

TEST_CASE("mutex")
{
    coral::mutex m;

    coral::mutex::awaiter_node first, second, third;

    m.try_lock(&first);

    SECTION("lock/unlock")
    {
        // state: mutex is locked with empty queue
        // conclusion: the coroutine captured the mutex and ready to go
        REQUIRE(first.next == coral::mutex::unlocked);

        const auto* next = m.try_unlock();
        // mutex is unlocked
        REQUIRE(next == coral::mutex::locked);

        SECTION("lock/unlock/lock")
        {
            m.try_lock(&second);

            // state: mutex is locked with empty queue
            // conclusion: the coroutine captured the mutex and ready to go
            REQUIRE(second.next == coral::mutex::unlocked);
        }
    }
    SECTION("lock/lock")
    {
        m.try_lock(&second);

        // state: mutex is locked with `second` awaiter in the queue
        // conclusion: awaiter is not ready to go and it is the only element in the queue
        REQUIRE(second.next == coral::mutex::locked);

        SECTION("lock/lock/unlock")
        {
            const auto* next = m.try_unlock();

            // state: mutex is locked with empty queue
            // conclusion: next coroutine is ready to go
            REQUIRE(next == &second);

            SECTION("lock/lock/unlock/lock")
            {
                m.try_lock(&third);

                // state: mutex is locked with `third` coroutine in the queue
                // conclusion: awaiter is not ready to go and it is the only element in the queue
                REQUIRE(third.next == coral::mutex::locked);
            }
            SECTION("lock/lock/unlock/unlock")
            {
                const auto* next2 = m.try_unlock();

                // state:: mutex is free
                REQUIRE(next2 == coral::mutex::locked);
            }
        }
        SECTION("lock/lock/lock")
        {
            m.try_lock(&third);

            // state: mutex is locked and `third` is the head of the queue
            // conclusion: `second` is the next awaiter
            REQUIRE(third.next == &second);

            SECTION("lock/lock/lock/unlock")
            {
                const auto* next = m.try_unlock();

                // state: mutex is locked with empty queue
                // conclusion: next coroutine is ready to go
                REQUIRE(next == &third);

                SECTION("lock/lock/lock/unlock/lock")
                {
                    coral::mutex::awaiter_node fourth;
                    m.try_lock(&fourth);

                    // state: mutex is locked with `third` coroutine in the queue
                    // conclusion: awaiter is not ready to go and it is the only element in the
                    // queue
                    REQUIRE(fourth.next == coral::mutex::locked);
                }
                SECTION("lock/lock/lock/unlock/unlock")
                {
                    const auto* next2 = m.try_unlock();

                    // state::mutex is free
                    REQUIRE(next2 == coral::mutex::locked);
                }
            }
        }
    }
}

TEST_CASE("unique_lock")
{
    coral::mutex m;
    int shared = 1;

    SECTION("lock/unlock/lock/unlock in a row")
    {
        auto task = [&m]() -> coral::task<> {
            {
                auto lock = co_await coral::when_locked(m);
            }
            {
                auto lock = co_await coral::when_locked(m);
            }
        };
        coral::sync_wait(task());
    }
    SECTION("second task is waiting for the other")
    {
        auto first_task = [&m, &shared]() -> coral::task<> {
            auto lock = co_await coral::when_locked(m);
            co_await async_delay(50ms);
            shared += 99;
        };
        auto second_task = [&m, &shared]() -> coral::task<> {
            auto lock = co_await coral::when_locked(m);
            shared *= 2;
        };
        coral::sync_wait(coral::when_all(first_task(), second_task()));
        REQUIRE(shared == 200);
    }
    SECTION("first task is waiting for the other")
    {
        auto first_task = [&m, &shared]() -> coral::task<> {
            co_await async_delay(1ms);
            auto lock = co_await coral::when_locked(m);
            shared *= 2;
        };
        auto second_task = [&m, &shared]() -> coral::task<> {
            auto lock = co_await coral::when_locked(m);
            co_await async_delay(50ms);
            shared += 99;
        };
        coral::sync_wait(coral::when_all(first_task(), second_task()));
        REQUIRE(shared == 200);
    }
}

TEST_CASE("multithreading test")
{
    coral::mutex m;
    static_thread_pool thread_pool(2);

    SECTION("sync_scheduler")
    {
        int shared = 0;
        constexpr size_t cycle = 100;
        constexpr size_t coroutines = 100;

        auto task = [&m, &shared, scheduler = thread_pool.scheduler()]() -> coral::task<> {
            for (size_t i = 0; i < cycle; ++i) {
                co_await scheduler;
                auto lock = co_await coral::when_locked(m);
                shared += 1;
            }
        };

        std::vector<coral::task<>> tasks;
        for (size_t i = 0; i < coroutines; ++i) {
            tasks.push_back(task());
        }

        coral::sync_wait(coral::when_all(tasks));

        REQUIRE(shared == cycle * coroutines);
    };
    SECTION("pool_scheduler")
    {
        int shared = 0;
        constexpr size_t cycle = 100;
        constexpr size_t coroutines = 100;

        auto task = [&m, &shared, scheduler = thread_pool.scheduler()]() -> coral::task<> {
            for (size_t i = 0; i < cycle; ++i) {
                co_await scheduler;
                auto lock = co_await coral::when_locked(m, scheduler);
                shared += 1;
            }
        };

        std::vector<coral::task<>> tasks;
        for (size_t i = 0; i < coroutines; ++i) {
            tasks.push_back(task());
        }

        coral::sync_wait(coral::when_all(tasks));

        REQUIRE(shared == cycle * coroutines);
    };
}