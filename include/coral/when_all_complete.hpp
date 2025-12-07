#pragma once
#include <coral/async_result.hpp>
#include <coral/concepts.hpp>
#include <coral/detail/adapter_task.hpp>
#include <coral/traits.hpp>

#include <algorithm>
#include <atomic>
#include <coroutine>
#include <functional>
#include <ranges>
#include <utility>

namespace coral {
namespace detail {

template <typename... AdapterTasks>
struct when_all_complete_task {
    when_all_complete_task(std::tuple<AdapterTasks...>&& tasks) noexcept
        : tasks_(std::move(tasks))
    {
    }

    when_all_complete_task(when_all_complete_task&&) noexcept = default;
    when_all_complete_task& operator=(when_all_complete_task&&) noexcept = default;

    when_all_complete_task(const when_all_complete_task&) = delete;
    when_all_complete_task& operator=(const when_all_complete_task&) = delete;

    auto operator co_await() noexcept
    {
        class awaiter {
            std::tuple<AdapterTasks...> tasks_;
            std::atomic<size_t> counter_;
            std::coroutine_handle<> continuation_;

        public:
            awaiter(std::tuple<AdapterTasks...>&& tasks)
                : tasks_(std::move(tasks))
                , counter_{std::tuple_size_v<std::tuple<AdapterTasks...>>}
            {
            }

            bool await_ready() noexcept { return false; }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                this->continuation_ = continuation;

                auto callback = std::bind_front(&awaiter::on_ready, this);
                [this, &callback]<std::size_t... Is>(std::index_sequence<Is...>) {
                    (std::get<Is>(tasks_).start(callback), ...);
                }(std::make_index_sequence<sizeof...(AdapterTasks) - 1>{});

                auto& last = std::get<sizeof...(AdapterTasks) - 1>(tasks_);
                return last.setup(std::move(callback));
            }

            auto await_resume()
            {
                return std::apply(
                    [](auto&... tasks) { return std::tuple{tasks.get_result()...}; }, tasks_);
            }

            std::coroutine_handle<> on_ready([[maybe_unused]] bool success)
            {
                if (counter_.fetch_sub(1) == 1) {
                    return std::move(continuation_);
                }
                return std::noop_coroutine();
            }
        };
        return awaiter{std::move(tasks_)};
    }

private:
    std::tuple<AdapterTasks...> tasks_;
};

template <typename Result>
struct when_all_complete_task_range {
    when_all_complete_task_range(std::vector<adaptor_task<Result>>&& tasks) noexcept
        : tasks_(std::move(tasks))
    {
    }

    when_all_complete_task_range(when_all_complete_task_range&&) noexcept = default;
    when_all_complete_task_range& operator=(when_all_complete_task_range&&) noexcept = default;

    when_all_complete_task_range(const when_all_complete_task_range&) = delete;
    when_all_complete_task_range& operator=(const when_all_complete_task_range&) = delete;

    auto operator co_await() noexcept
    {
        class awaiter {
            std::vector<adaptor_task<Result>> tasks_;
            std::atomic<size_t> counter_;
            std::coroutine_handle<> continuation_;

        public:
            awaiter(std::vector<adaptor_task<Result>>&& tasks)
                : tasks_(std::move(tasks))
                , counter_{tasks_.size()}
            {
            }

            bool await_ready() noexcept { return tasks_.empty(); }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                this->continuation_ = continuation;

                auto callback = std::bind_front(&awaiter::on_ready, this);

                for (size_t i = 0; i < tasks_.size() - 1; ++i) {
                    tasks_[i].start(callback);
                }

                auto& last = tasks_.back();
                return last.setup(std::move(callback));
            }

            auto await_resume()
            {
                std::vector<async_result<Result>> results;
                results.reserve(tasks_.size());
                for (auto& task : tasks_) {
                    results.push_back(task.get_result());
                }
                return results;
            }

            std::coroutine_handle<> on_ready([[maybe_unused]] bool success)
            {
                if (counter_.fetch_sub(1) == 1) {
                    return std::move(continuation_);
                }
                return std::noop_coroutine();
            }
        };
        return awaiter{std::move(tasks_)};
    }

private:
    std::vector<adaptor_task<Result>> tasks_;
};

} // namespace detail

template <awaitable... Awaitable>
auto when_all_complete(Awaitable&&... awaitables)
{
    auto tasks = detail::make_adapter_tasks(std::forward<Awaitable>(awaitables)...);
    return detail::when_all_complete_task(std::move(tasks));
}

template <typename Range>
    requires std::ranges::range<Range> && awaitable<std::ranges::range_value_t<Range>>
auto when_all_complete(Range&& awaitables)
{
    auto tasks = detail::make_adapter_tasks(std::forward<Range>(awaitables));
    return detail::when_all_complete_task_range(std::move(tasks));
}

} // namespace coral