# Examples for coral library

| target | description|
| - | - |
| example_01 | Demonstrates racing multiple coroutines with `when_any`, cooperative cancellation via `std::stop_token`, signal handling with `when_signal`, and custom awaitable implementation. Runs directory enumeration until timeout or Ctrl+C. |
| example_02 | Demonstrates `coral::nursery` for managing dynamic child coroutines, `nursery_task` for structured concurrency, and `when_stopped` awaitable. Simulates a server spawning client handlers with proper cancellation propagation. |