# Coral

A header-only C++20/23 **cor**outine **a**bstraction **l**ibrary

[![CI](https://github.com/subbota-a/coral/actions/workflows/ci.yml/badge.svg)](https://github.com/subbota-a/coral/actions/workflows/ci.yml)

## Why Another Coroutine Library?

Coral is designed for integrating coroutines into **existing projects**. 
You've finally decided to refactor your existing complex asynchronous code to use C++20 coroutines and want to reuse as much of your existing codebase as possible.
You only need the coroutine primitives — nothing more, nothing less.

**Why not use existing libraries?** They often include features you don't need in an **existing** project, along with other critical issues:

- **Lack of extensibility** - Difficult to integrate with other coroutine primitives (e.g., asio)
- **Lack of [structured concurrency](https://ericniebler.com/2020/11/08/structured-concurrency)** - Essential thing to write reliable async code (e.g. asio, libcoro)
- **Abandoned maintenance** - e.g., cppcoro
- **Application-level features** - Libraries bundle network I/O, file I/O, timers, schedulers, thread pools, etc — bringing extra dependencies to OS or other libraries. (e.g. libcoro, consurrentcpp, ...)

For new projects, a full-featured library might be perfect. But for existing codebases, **additional features introduce unnecessary dependencies**. 
Coral provides just the primitives, letting you integrate with your existing infrastructure (or compose with libraries like stdexec for executors).

## Features

- **Depends on STL only** - No external libraries required
- **Pure Coroutine Primitives** - No threading, no executors, no I/O, no OS dependencies - just coroutines
- **Structured concurrency** - Coroutines never exit earlier than children coroutines
- **Cooperative cancellation** - Integration with `std::stop_token`
- **Special support for `std::expected`** - cancellation respects `expected` error
- **Header-Only** - Easy integration into any project

## Quick Primitives Overview

- **[`task<T>`](#taskt)** - Lazy coroutine task
- **[`nursery_task<T>`](#nursery_taskt)** - Lazy coroutine task with nursery
- **[`sync_wait()`](#sync_wait)** - Runs coroutine in the current thread, blocks if needed
- **[`when_all_complete()`](#when_all_complete)** - All results: waits for all coroutines, returns tuple of results
- **[`when_all()`](#when_all)** - All or nothing: returns all successes or first error
- **[`when_any()`](#when_any)** - First success: returns first successful result
- **[`when_stopped()`](#when_stopped)** - Waits for cancellation via `std::stop_token`
- **[`when_signal()`](#when_signal)** - Waits for ctrl+c (SIGINT) or (SIGTERM) signal
- **[`mutex`, `when_locked()`, `unique_lock`](#mutex-when_locked-unique_lock)** - Async mutex for coroutine synchronization
- **[`scheduler` concept](#scheduler-concept)** - Concept for types that can schedule coroutine execution
- **[`single_event<T>`](#single_eventt)** - One-shot event for passing a value from producer to consumer
- **[`generator<T>`](#generatort)** - Synchronous generator that yields values lazily

## Requirements

- C++20 compatible compiler (tested on):
  - GCC 14+
  - Clang 18+
  - MSVC 19.44+ (Visual Studio 2022)
- CMake 3.20+ (for building/installing)

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## API Overview

### `task<T>`

A lazy coroutine task that begins execution only when awaited. Supports value types, references, and void.

```c++
#include <coral/task/hpp>

coral::task<int> sum(int a, int b) {
  co_return a + b;
}
```

### `nursery_task<T>`

Similar to `task<T>`, but provides access to a `nursery` object that allows spawning an unlimited number of child coroutines in a "fire-and-forget" style in 
terms of parent doesn't receive the result of child coroutines execution. But `nursery_task<T>` completes only after all children complete. 

_Causion!_ Nursery coroutine's local variables are destroyed before all children coroutine completes,
so don't pass references to local variables to child coroutines.

```c++
#include <coral/nursery.hpp>
#include <coral/task.hpp>
#include <coral/when_signal.hpp>

coral::task<> client(socket s, std::stop_token stop_token);

coral::nursery_task<bool> server(socket s, std::stop_token stop_token) {
  std::stop_source stop_source;
  std::stop_callback callback(stop_token, [&stop_source]{ stop_source.request_stop(); });

  auto nursery = co_await coral::get_nursery();

  while(!stop_token.stop_requested()){
    auto [connection, error] = co_await accept(s, stop_token); // out of the scope coral library
    if (error) { // if accept fails then cancel all
      stop_source.request_stop();
      co_return false; // completes with error
    }
    // Add client coroutine to the nursery collection
    nursery.start(client(std::move(connection), stop_token.get_token()));
  }
  co_return true; // completes by stop_token request
}

```

### `sync_wait()`

```cpp
auto sync_wait(awaitable auto&& awaitable) // exposition only
```

Starts coroutine execution in the current thread. If the coroutine doesn't complete synchronously,
blocks the current thread until completion. Returns the coroutine result or rethrows any exception.


```cpp
#include <coral/sync_wait.hpp>
#include <coral/task.hpp>

coral::task<int> sum(int a, int b);

int main() {
  int result = coral::sync_wait(sum(1, 2));
}
```

### `when_all_complete()`

```cpp
// Exposition only signature

std::tuple<coral::async_result<T>, ...> when_all_complete(awaitable auto&&... awaitables) 

std::vector<coral::async_result<T>> when_all_complete(std::ranges::range auto&& awaitables)
```

Starts all coroutines in the order they are listed and asynchronously waits for all to complete.
Returns a tuple of `async_result` values containing either the successful result or an exception for each coroutine.

```cpp
#include <coral/when_all_complete.hpp>
#include <coral/task.hpp>

coral::task<int> fetch_a();
coral::task<int> fetch_b();

coral::task<void> variadic_example() {
  std::tuple<async_result<int>, async_result<int>> = 
    co_await coral::when_all_complete(fetch_a(), fetch_b());
}

coral::task<void> range_example(std::vector<coral::task<int>> tasks) {
  std::vector<coral::async_result<int>> results = 
    co_await coral::when_all_complete(std::move(tasks));
}
```

### `when_all()`

```cpp
// Exposition only signature

std::tuple<T, ...> when_all(awaitable auto&&... awaitables)
std::tuple<T, ...> when_all(std::stop_source&, awaitable auto&&... awaitables)

std::vector<T> when_all(std::ranges::range auto&& awaitables)
std::vector<T> when_all(std::stop_source&, std::ranges::range auto&& awaitables)
```

All-or-nothing semantics.
Returns all successful results or the first error.
Starts coroutines sequentially in the order listed;
if any coroutine fails before the remaining ones are started, those won't be started.
The `stop_source` variant requests cancellation as soon as any coroutine fails.
Tasks returning `std::expected` can fail via `unexpected` or by throwing an exception.
Other tasks can only fail by throwing an exception.
The first error is returned via exception if it was an exception,
or as `std::expected` if the task returned `unexpected`.
The caller resumes on the thread that completed the last task. 

```cpp
#include <coral/when_all.hpp>
#include <coral/task.hpp>

coral::task<int> fetch_a();
coral::task<void> fetch_b();
coral::task<std::expected<int, std::error_code>> fetch_or_error();

coral::task<void> variadic_example() 
{
  std::tuple<int, std::monostate> result = co_await coral::when_all(fetch_a(), fetch_b());
}

coral::task<void> variadic_example_with_expected() 
{
  std::expected<std::tuple<int, int>, std::variant<std::error_code>> result = 
    co_await coral::when_all(fetch_a(), fetch_or_error());
}

coral::task<void> range_example(std::vector<coral::task<int>> tasks) 
{
  std::vector<int> results = co_await coral::when_all(std::move(tasks));
}

coral::task<void> range_example_with_void(std::vector<coral::task<void>> tasks) 
{
  /* returns void */ co_await coral::when_all(std::move(tasks));
}

coral::task<void> range_example_with_expected(
  std::vector<coral::task<std::expected<int, std::error_code>>> tasks) 
{
  std::expected<std::vector<int>, std::error_code> result = 
    co_await coral::when_all(std::move(tasks));
}

coral::task<void> range_example_with_expected_void(
  std::vector<coral::task<std::expected<void, std::error_code>>> tasks) 
{
  std::expected<void, std::error_code> result = 
    co_await coral::when_all(std::move(tasks));
}

```

### `when_any()`

```cpp
// Exposition only signature

std::pair<size_t, std::variant<T, ...>> when_any(awaitable auto&&... awaitables)
std::pair<size_t, std::variant<T, ...>> when_any(std::stop_source&, awaitable auto&&... awaitables)

std::pair<size_t, T> when_any(std::ranges::range auto&& awaitables)
std::pair<size_t, T> when_any(std::stop_source&, std::ranges::range auto&& awaitables)
```

First-success semantics.
Returns the first successful result along with its index, or the first error if all fail.
Starts coroutines sequentially in the order listed;
if any coroutine succeeds before the remaining ones are started, those won't be started.
The `stop_source` variant requests cancellation as soon as any coroutine succeeds.
Tasks returning `std::expected` can fail via `unexpected` or by throwing an exception.
Other tasks can only fail by throwing an exception.
The first error is returned via exception if it was an exception,
or as `std::expected` with index pointing to the first failed coroutine if the task returned `unexpected`.
The caller resumes on the thread that completed the last task.

```cpp
#include <coral/when_any.hpp>
#include <coral/task.hpp>

coral::task<int> fetch_a();
coral::task<std::string> fetch_b();
coral::task<std::expected<int, std::error_code>> fetch_or_error();

coral::task<void> variadic_example()
{
  std::pair<size_t, std::variant<int, std::string>> result =
    co_await coral::when_any(fetch_a(), fetch_b());
}

coral::task<void> variadic_example_with_expected()
{
  std::pair<size_t, std::expected<std::variant<int, int>, std::variant<std::error_code>>> result =
    co_await coral::when_any(fetch_a(), fetch_or_error());
}

coral::task<void> range_example(std::vector<coral::task<int>> tasks)
{
  std::pair<size_t, int> result = co_await coral::when_any(std::move(tasks));
}

coral::task<void> range_example_with_void(std::vector<coral::task<void>> tasks)
{
  size_t index = co_await coral::when_any(std::move(tasks));
}

coral::task<void> range_example_with_expected(
  std::vector<coral::task<std::expected<int, std::error_code>>> tasks)
{
  std::pair<size_t, std::expected<int, std::error_code>> result =
    co_await coral::when_any(std::move(tasks));
}
```

### `when_stopped()`

```cpp
// Exposition only signature

awaitable auto when_stopped(std::stop_token stop_token)
```

Returns an awaitable that completes when `stop_token.stop_requested()` becomes true.
If already requested, completes immediately without suspending.
The caller resumes on the thread that called `stop_source.request_stop()`.

```cpp
#include <coral/when_stopped.hpp>
#include <coral/task.hpp>

coral::task<void> wait_for_cancellation(std::stop_token stop_token)
{
  co_await coral::when_stopped(stop_token);
  // Cancellation requested
}
```

### `when_signal()`

```cpp
// Exposition only signature

awaitable auto when_signal(std::stop_token stop_token, int sig)
```

Returns an awaitable that completes when the specified signal is raised (e.g., SIGINT, SIGTERM) or when `stop_token.stop_requested()` becomes true.
Spawns a dedicated thread to wait for the signal.
Only one `when_signal` can be active at a time; throws `std::runtime_error` if called while another is active.
When complete, restores the default signal handler (`SIG_DFL`).
The caller resumes on the dedicated thread.

```cpp
#include <coral/when_signal.hpp>
#include <coral/task.hpp>
#include <csignal>

coral::task<void> wait_for_interrupt(std::stop_token stop_token)
{
  co_await coral::when_signal(stop_token, SIGINT);
  // Signal received or stop requested
}
```

### `mutex`, `when_locked()`, `unique_lock`

```cpp
// Exposition only signature

class mutex; // non-copyable, non-movable

unique_lock<Scheduler> when_locked(mutex& m)
unique_lock<Scheduler> when_locked(mutex& m, Scheduler scheduler)
```

Async mutex for coordinating access to shared resources between coroutines.
Unlike `std::mutex`, waiting coroutines suspend instead of blocking threads.
`mutex` is non-copyable and non-movable.
Waiting coroutines are stored in a LIFO stack.

```cpp
#include <coral/mutex.hpp>

coral::mutex m;
auto lock = co_await coral::when_locked(m);
// critical section
// lock automatically released on destruction
```

#### How It Works

When a coroutine calls `co_await when_locked(m)`:
1. If the mutex is free — the coroutine acquires it immediately and continues
2. If the mutex is locked — the coroutine suspends and joins a wait queue (LIFO stack)

When `unlock()` is called (explicitly or via `unique_lock` destructor):
1. If there are waiting coroutines — the next waiter is resumed via `scheduler.schedule(handle)`
2. If no waiters — the mutex becomes free

#### Default Behavior (sync_scheduler)

By default, `when_locked(m)` uses `sync_scheduler` which calls the waiter's coroutine handle directly.
This means the **waiting coroutine resumes synchronously on the same thread** that called `unlock()`. This creates a call stack:

```
Thread 1:
  A.unlock()
    → B.resume() → B runs → B.unlock()
        → C.resume() → C runs → C.unlock()
            → ...
```

**Implication**: With N waiting coroutines, the call stack depth grows to N. This is efficient (no context switches) but may cause stack overflow with many waiters.

#### Custom Scheduler

You can provide a custom scheduler to control how waiters are resumed:

```cpp
auto lock = co_await coral::when_locked(m, my_scheduler);
```

The scheduler must satisfy [`coral::scheduler`](#scheduler-concept) concept.

With a thread pool scheduler:
- `unlock()` posts the waiting coroutine to the thread pool queue
- The current coroutine returns immediately from `unlock()`

### `scheduler` concept

Concept for types that can schedule coroutine execution. Defined in `<coral/concepts.hpp>`.

```cpp
template<typename T>
concept scheduler = requires(T& s, std::coroutine_handle<> h) {
    s.schedule(h);
};
```

A scheduler is any type with a `schedule` method that accepts a coroutine handle. The scheduler decides when and where to resume the coroutine:

- **Synchronous scheduler** — calls `h()` directly, resuming on the current thread
- **Thread pool scheduler** — posts `h` to a queue, resuming on a worker thread
- **Event loop scheduler** — posts `h` to an event loop (e.g., Qt, libuv, asio)

Coral provides `sync_scheduler` as the default:

```cpp
struct sync_scheduler {
    void schedule(std::coroutine_handle<> h) const { h(); }
};
```

### `single_event<T>`

```cpp
// Exposition only signature

template<typename T = void>
class single_event {
  sender get_sender();
  awaitable get_awaitable();
};

class single_event<T>::sender{  // move-only
  void set_value(T&&);
  void set_exception(std::exception_ptr);
};
```

One-shot event primitive for passing a value (or just a signal) from a producer to a waiting consumer coroutine. Inspired by `std::promise`/`std::future` semantics but designed for coroutines.

#### Contract

`single_event<T>` transfers a single value of type `T` (or a signal for `T=void`) from producer to consumer.

**Ownership and lifetime:**
- `single_event` holds the state and is awaitable (consumer side)
- `sender` is a lightweight move-only reference to `single_event` (producer side)
- `single_event` must outlive all operations on it
- Zero allocations — all state is stored inside `single_event`

**Order of operations:**
- `set_value()` or `set_exception` can be called before or after `co_await`
- If value is already set, `co_await` returns immediately
- If value is not yet set, `co_await` suspends the coroutine

**Restrictions:**
- `get_sender()` can only be called when no sender exists
- `set_value()` / `set_exception()` can only be called once per sender
- `co_await` or `get_awaitable` can only be called once
- Value is moved out on `co_await` (move-only types are supported)
- If `co_await` is called without an active sender (not created or destroyed without `set_xxx()`), `single_event_error` is thrown
- If sender destroys without calling `set_xxx` when consumer is suspended in `co_await` the consumer is resumed and `single_event_error` is thrown

#### Basic Usage

```cpp
#include <coral/single_event.hpp>
#include <coral/task.hpp>

coral::task<int> async_computation();

coral::task<void> example() {
    coral::single_event<int> event;

    // Get sender and pass to producer
    auto sender = event.get_sender();

    // Producer sets value (could be in another coroutine)
    sender.set_value(42);

    // Consumer awaits
    int result = co_await event;  // result == 42
}
```

#### Producer and Consumer
```cpp
#include <coral/single_event.hpp>
#include <coral/task.hpp>
#include <coral/when_all.hpp>

coral::task<> producer(coral::single_event<> sender);

coral::task<> example() {
  coral::single_event<> event;

  co_await coral::when_all(event.get_awaitable(), producer(event.get_sender()));
}
```

#### Integrating with Callbacks

Common pattern for wrapping callback-based APIs:

```cpp
coral::task<Response> async_http_get(const std::string& url) {
    coral::single_event<Response> event;

    // Start async operation with callback
    http_client.get(url, [sender = event.get_sender()](Result result) mutable {
        if (result.success) {
            sender.set_value(std::move(result.response));
        } else {
            sender.set_exception(
                std::make_exception_ptr(std::runtime_error(result.error)));
        }
    });

    co_return co_await event;
}
```

### `generator<T>`

```cpp
// Exposition only signature

template <typename Ref, typename V = void>
class generator; // models std::ranges::input_range
```

Synchronous generator coroutine that yields values lazily. Models `std::ranges::input_range` and works seamlessly with range-based for loops and standard algorithms.

Unlike `task<T>` which produces a single value, `generator<T>` can produce a sequence of values on-demand using `co_yield`.

#### Template Parameters

- **`Ref`** - The type returned when dereferencing the iterator (`*it`). This is what the user receives from the generator.
- **`V`** - Value type (defaults to `std::remove_cvref_t<Ref>`). Rarely needed — use it when `value_type` should differ from the dereferenced type, e.g., for proxy iterators like `vector<bool>` where `reference` is a proxy but `value_type` is `bool`.

Common instantiations:
- `generator<int>` - yields `int` values
- `generator<int&>` - yields mutable references to existing objects
- `generator<const int&>` - yields const references

#### Basic Usage

```cpp
#include <coral/generator.hpp>

coral::generator<int> iota(int n) {
    for (int i = 0; i < n; ++i) {
        co_yield i;
    }
}

void example() {
    for (int x : iota(5)) {
        // x = 0, 1, 2, 3, 4
    }
}
```

#### Yielding References

```cpp
coral::generator<int&> enumerate(std::vector<int>& vec) {
    for (auto& elem : vec) {
        co_yield elem;  // yields reference to element
    }
}

void modify_all(std::vector<int>& vec) {
    for (int& x : enumerate(vec)) {
        x *= 2;  // modifies original vector
    }
}
```

#### Using with Ranges

```cpp
#include <ranges>

coral::generator<int> iota(int n) {
    for (int i = 0; i < n; ++i) {
        co_yield i;
    }
}

void example() {
    // First 5 even squares using range adaptors
    for (int x : iota(20)
                 | std::views::transform([](int x) { return x * x; })
                 | std::views::filter([](int x) { return x % 2 == 0; })
                 | std::views::take(5)) {
        // x = 0, 4, 16, 36, 64
    }

    // Using with C++23 algorithms
    int sum = std::ranges::fold_left(iota(10), 0, std::plus<>{});
    // sum = 45
}
```