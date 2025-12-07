#include <coral/single_event.hpp>
#include <coral/sync_wait.hpp>
#include <coral/task.hpp>
#include <coral/when_all.hpp>

#include "helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_exception.hpp>

#include <memory>
#include <string>

using namespace std::chrono_literals;
using Catch::Matchers::Message;

TEST_CASE("single_event<int> basic")
{
    SECTION("set_value before co_await")
    {
        coral::single_event<int> event;
        auto sender = event.get_sender();
        sender.set_value(42);

        REQUIRE(coral::sync_wait(event.get_awaitable()) == 42);
    }
    SECTION("set_exception before co_await")
    {
        coral::single_event<int> event;
        auto sender = event.get_sender();
        sender.set_exception(std::make_exception_ptr(std::runtime_error("test error")));

        REQUIRE_THROWS_MATCHES(
            coral::sync_wait(event.get_awaitable()), std::runtime_error, Message("test error"));
    }

    SECTION("co_await before set_value")
    {
        coral::single_event<int> event;
        auto sender = event.get_sender();

        auto producer = [](coral::single_event<int>::sender s) -> coral::task<> {
            s.set_value(42);
            co_return;
        };

        auto consumer = [&event]() -> coral::task<int> { co_return co_await event; };

        auto [result, _] =
            coral::sync_wait(coral::when_all(consumer(), producer(std::move(sender))));

        REQUIRE(result == 42);
    }
    SECTION("co_await before set_exception")
    {
        coral::single_event<int> event;
        auto sender = event.get_sender();

        auto producer = [](coral::single_event<int>::sender s) -> coral::task<> {
            s.set_exception(std::make_exception_ptr(std::runtime_error("test error")));
            co_return;
        };

        auto consumer = [&event]() -> coral::task<int> { co_return co_await event; };

        REQUIRE_THROWS_MATCHES(
            coral::sync_wait(coral::when_all(consumer(), producer(std::move(sender)))),
            std::runtime_error,
            Message("test error"));
    }
}

TEST_CASE("single_event<void> basic")
{
    SECTION("set_value before co_await")
    {
        coral::single_event<void> event;
        auto sender = event.get_sender();
        sender.set_value();

        coral::sync_wait(event.get_awaitable());
    }
    SECTION("set_exception before co_await")
    {
        coral::single_event<void> event;
        auto sender = event.get_sender();
        sender.set_exception(std::make_exception_ptr(std::runtime_error("test error")));

        REQUIRE_THROWS_MATCHES(
            coral::sync_wait(event.get_awaitable()), std::runtime_error, Message("test error"));
    }

    SECTION("co_await before set_value")
    {
        coral::single_event<void> event;
        auto sender = event.get_sender();

        auto producer = [](coral::single_event<void>::sender s) -> coral::task<> {
            s.set_value();
            co_return;
        };

        auto consumer = [&event]() -> coral::task<> { co_await event; };

        coral::sync_wait(coral::when_all(consumer(), producer(std::move(sender))));
    }
    SECTION("co_await before set_exception")
    {
        coral::single_event<void> event;
        auto sender = event.get_sender();

        auto producer = [](coral::single_event<void>::sender s) -> coral::task<> {
            s.set_exception(std::make_exception_ptr(std::runtime_error("test error")));
            co_return;
        };

        auto consumer = [&event]() -> coral::task<> { co_await event; };

        REQUIRE_THROWS_MATCHES(
            coral::sync_wait(coral::when_all(consumer(), producer(std::move(sender)))),
            std::runtime_error,
            Message("test error"));
    }
}

TEST_CASE("single_event no_sender_error")
{
    SECTION("co_await without sender")
    {
        coral::single_event<int> event;

        auto task = [&event]() -> coral::task<int> { co_return co_await event; };

        REQUIRE_THROWS_AS(coral::sync_wait(task()), coral::single_event_error);
    }

    SECTION("sender destroyed without set_value")
    {
        coral::single_event<int> event;

        {
            auto sender = event.get_sender();
            // sender destroyed here
        }

        auto task = [&event]() -> coral::task<int> { co_return co_await event; };

        REQUIRE_THROWS_AS(coral::sync_wait(task()), coral::single_event_error);
    }

    SECTION("co_await completes when sender destroyed without set_value")
    {
        coral::single_event<int> event;
        auto sender = event.get_sender();

        auto producer = [](coral::single_event<int>::sender s) -> coral::task<> {
            // does not call sender
            co_return;
        };

        auto consumer = [&event]() -> coral::task<int> { co_return co_await event; };

        REQUIRE_THROWS_AS(
            coral::sync_wait(coral::when_all(consumer(), producer(std::move(sender)))),
            coral::single_event_error);
    }
}

TEST_CASE("single_event with move-only types")
{
    SECTION("unique_ptr")
    {
        coral::single_event<std::unique_ptr<int>> event;
        auto sender = event.get_sender();
        sender.set_value(std::make_unique<int>(42));

        auto result = coral::sync_wait(event.get_awaitable());
        REQUIRE(result != nullptr);
        REQUIRE(*result == 42);
    }
}

TEST_CASE("single_event sender move semantics")
{
    SECTION("move sender")
    {
        coral::single_event<int> event;
        auto sender1 = event.get_sender();
        auto sender2 = std::move(sender1);
        sender2.set_value(42);

        auto task = [&event]() -> coral::task<int> { co_return co_await event; };

        REQUIRE(coral::sync_wait(task()) == 42);
    }

    SECTION("move assign sender")
    {
        coral::single_event<int> event1;
        coral::single_event<int> event2;

        auto sender1 = event1.get_sender();
        auto sender2 = event2.get_sender();

        sender1 = std::move(sender2);
        // sender1 (pointing to event1) is released, sets no_sender_error on event1
        // sender1 now points to event2

        sender1.set_value(42);

        auto task1 = [&event1]() -> coral::task<int> { co_return co_await event1; };
        auto task2 = [&event2]() -> coral::task<int> { co_return co_await event2; };

        REQUIRE_THROWS_AS(coral::sync_wait(task1()), coral::single_event_error);
        REQUIRE(coral::sync_wait(task2()) == 42);
    }
}

TEST_CASE("single_event async producer")
{
    SECTION("producer in separate coroutine")
    {
        coral::single_event<int> event;
        auto sender = event.get_sender();

        auto producer = [](coral::single_event<int>::sender s) -> coral::task<> {
            co_await async_delay(10ms);
            s.set_value(42);
            co_return;
        };

        auto consumer = [&event]() -> coral::task<int> { co_return co_await event; };

        auto [result, _] =
            coral::sync_wait(coral::when_all(consumer(), producer(std::move(sender))));

        REQUIRE(result == 42);
    }
}

TEST_CASE("single_event sender double call")
{
    SECTION("set_value twice")
    {
        coral::single_event<int> event;
        auto sender = event.get_sender();

        sender.set_value(42);
        REQUIRE_THROWS_AS(sender.set_value(100), coral::single_event_error);

        REQUIRE(coral::sync_wait(event.get_awaitable()) == 42);
    }

    SECTION("set_exception twice")
    {
        coral::single_event<int> event;
        auto sender = event.get_sender();

        sender.set_exception(std::make_exception_ptr(std::runtime_error("first")));
        REQUIRE_THROWS_AS(
            sender.set_exception(std::make_exception_ptr(std::runtime_error("second"))),
            coral::single_event_error);

        REQUIRE_THROWS_MATCHES(
            coral::sync_wait(event.get_awaitable()), std::runtime_error, Message("first"));
    }

    SECTION("set_value then set_exception")
    {
        coral::single_event<int> event;
        auto sender = event.get_sender();

        sender.set_value(42);
        REQUIRE_THROWS_AS(
            sender.set_exception(std::make_exception_ptr(std::runtime_error("error"))),
            coral::single_event_error);

        REQUIRE(coral::sync_wait(event.get_awaitable()) == 42);
    }

    SECTION("set_exception then set_value")
    {
        coral::single_event<int> event;
        auto sender = event.get_sender();

        sender.set_exception(std::make_exception_ptr(std::runtime_error("error")));
        REQUIRE_THROWS_AS(sender.set_value(42), coral::single_event_error);

        REQUIRE_THROWS_MATCHES(
            coral::sync_wait(event.get_awaitable()), std::runtime_error, Message("error"));
    }
}