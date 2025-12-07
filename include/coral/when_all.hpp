#pragma once
#include <coral/async_result.hpp>
#include <coral/concepts.hpp>
#include <coral/detail/adapter_task.hpp>
#include <coral/detail/adaptor_task_helper.hpp>
#include <coral/detail/traits.hpp>
#include <coral/traits.hpp>

#include <atomic>
#include <coroutine>
#include <expected>
#include <functional>
#include <stop_token>
#include <type_traits>
#include <utility>

namespace coral {
namespace detail {

inline std::monostate get_non_void_task_result(adaptor_task<void>& task)
{
    task.get_result_value();
    return {};
}

template <typename T>
    requires(!std::is_void_v<T>)
decltype(auto) get_non_void_task_result(adaptor_task<T>& task)
{
    return task.get_result_value();
}

template <typename... AdaptorTasks>
struct when_all_task {
    when_all_task(std::stop_source* stop_source,
        std::tuple<AdaptorTasks...>&& tasks) noexcept
        : stop_source_(stop_source)
        , tasks_(std::move(tasks))
    {
    }

    when_all_task(when_all_task&&) noexcept = default;
    when_all_task& operator=(when_all_task&&) noexcept = default;

    when_all_task(const when_all_task&) = delete;
    when_all_task& operator=(const when_all_task&) = delete;

    auto operator co_await() noexcept
    {
        class awaiter {
            std::stop_source* stop_source_;
            std::tuple<AdaptorTasks...> tasks_;
            std::atomic<size_t> counter_;
            std::atomic<std::size_t> first_failed_task_index_;
            std::coroutine_handle<> continuation_;

        public:
            awaiter(std::stop_source* stop_source, std::tuple<AdaptorTasks...>&& tasks)
                : stop_source_(stop_source)
                , tasks_(std::move(tasks))
                , counter_{std::tuple_size_v<std::tuple<AdaptorTasks...>>}
                , first_failed_task_index_{std::tuple_size_v<std::tuple<AdaptorTasks...>>}
            {
            }

            bool await_ready() noexcept { return false; }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                continuation_ = continuation;

                constexpr auto task_count = sizeof...(AdaptorTasks);
                size_t started_tasks = 0;

                const auto start_task = [this, &started_tasks]<std::size_t Index>(
                                            std::integral_constant<std::size_t, Index>) {
                    std::get<Index>(tasks_).start(std::bind_front(&awaiter::on_ready, this, Index));
                    started_tasks = Index + 1;
                    if (first_failed_task_index_.load(std::memory_order::acquire) <= Index) {
                        return false;
                    }
                    return true;
                };

                const auto ok = [&]<std::size_t... Is>(std::index_sequence<Is...>) {
                    return (start_task(std::integral_constant<std::size_t, Is>{}) && ...);
                }(std::make_index_sequence<task_count - 1>{});

                if (!ok) {
                    const auto left_to_run = task_count - started_tasks;
                    return counter_.fetch_sub(left_to_run) == left_to_run
                        ? continuation_
                        : std::noop_coroutine();
                }

                constexpr auto last_index = task_count - 1;
                auto& last = std::get<last_index>(tasks_);
                return last.setup(std::bind_front(&awaiter::on_ready, this, last_index));
            }

            auto await_resume()
            {
                constexpr auto task_count = sizeof...(AdaptorTasks);
                const auto failed_index = first_failed_task_index_.load(std::memory_order::relaxed);

                using task_results_t = tuple_of_adaptor_task_result_t<AdaptorTasks...>;
                using value_type = tuple_of_values_t<task_results_t>;

#if CORAL_EXPECTED
                if constexpr (has_any_expected_v<typename AdaptorTasks::result_type...>) {
                    using error_type = variant_of_errors_t<task_results_t>;
                    using expected_type = std::expected<value_type, error_type>;

                    if (failed_index < task_count) { // some task fail
                        return expected_type{
                            std::unexpect, extract_task_error<error_type>(failed_index, tasks_)};
                    } else {
                        auto tuple_result = std::apply(
                            [](auto&... tasks) -> std::tuple<decltype(get_non_void_task_result(tasks))...>{
                                return {get_non_void_task_result(tasks)...};
                            },
                            tasks_);
                        return expected_type{std::in_place, extract_values_for_tuple(std::move(tuple_result))};
                    }
                } else
#endif
                {
                    if (failed_index < task_count) {
                        using variant_type = unique_variant<typename AdaptorTasks::result_type...>;
                        std::ignore = extract_task_value<variant_type>(failed_index, this->tasks_);
#if __cpp_lib_unreachable >= 202202L
                        std::unreachable();
#else
                        throw std::logic_error("exception expected");
#endif
                    } else {
                        return std::apply(
                            [](auto&... tasks) {
                                return value_type{get_non_void_task_result(tasks)...};
                            },
                            tasks_);
                    }
                }
            }

            std::coroutine_handle<> on_ready(std::size_t index, bool success)
            {
                size_t expected = sizeof...(AdaptorTasks);
                if (!success
                    && first_failed_task_index_.compare_exchange_strong(
                        expected, index, std::memory_order::acq_rel)
                    && stop_source_) {
                    stop_source_->request_stop();
                }
                if (counter_.fetch_sub(1) == 1) {
                    return std::move(continuation_);
                }
                return std::noop_coroutine();
            }
        };
        return awaiter{stop_source_, std::move(tasks_)};
    }

private:
    std::stop_source* stop_source_;
    std::tuple<AdaptorTasks...> tasks_;
};

template <typename Result>
struct when_all_task_range {
    when_all_task_range(std::stop_source* stop_source,
        std::vector<adaptor_task<Result>>&& tasks) noexcept
        : stop_source_(stop_source)
        , tasks_(std::move(tasks))
    {
    }

    when_all_task_range(when_all_task_range&&) noexcept = default;
    when_all_task_range& operator=(when_all_task_range&&) noexcept = default;

    when_all_task_range(const when_all_task_range&) = delete;
    when_all_task_range& operator=(const when_all_task_range&) = delete;

    auto operator co_await() noexcept
    {
        class awaiter {
            std::stop_source* stop_source_;
            std::vector<adaptor_task<Result>> tasks_;
            std::atomic<size_t> counter_;
            std::atomic<std::size_t> first_failed_task_index_;
            std::coroutine_handle<> continuation_;

        public:
            awaiter(std::stop_source* stop_source, std::vector<adaptor_task<Result>>&& tasks)
                : stop_source_(stop_source)
                , tasks_(std::move(tasks))
                , counter_{tasks_.size()}
                , first_failed_task_index_{tasks_.size()}
            {
            }

            bool await_ready() noexcept { return tasks_.empty(); }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                continuation_ = continuation;

                for (size_t i = 0; i < tasks_.size() - 1; ++i) {
                    tasks_[i].start(std::bind_front(&awaiter::on_ready, this, i));
                    if (first_failed_task_index_.load(std::memory_order::acquire) <= i) {
                        // one task has failed already, not necessary to start the rest of the tasks
                        const auto left_to_run = tasks_.size() - i - 1;
                        const auto all_tasks_completes =
                            counter_.fetch_sub(left_to_run) == left_to_run;
                        return all_tasks_completes ? continuation_ : std::noop_coroutine();
                    }
                }
                const auto last_index = tasks_.size() - 1;
                return tasks_[last_index].setup(
                    std::bind_front(&awaiter::on_ready, this, last_index));
            }

            auto await_resume()
            {
#if CORAL_EXPECTED
                if constexpr (is_expected_v<Result>) {
                    using value_type = std::conditional_t<std::is_void_v<value_of<Result>>,
                        void,
                        std::vector<value_of<Result>>>;
                    using result_type = std::expected<value_type, error_of<Result>>;

                    const auto failed_index =
                        first_failed_task_index_.load(std::memory_order::acquire);

                    if (failed_index < tasks_.size()) { // some task fail
                        return result_type{
                            std::unexpect, tasks_[failed_index].get_result_value().error()};
                    } else {
                        if constexpr (std::is_void_v<value_type>) {
                            return result_type{};
                        } else {
                            value_type v;
                            v.reserve(tasks_.size());
                            for (auto& t : tasks_) {
                                v.push_back(t.get_result_value().value());
                            }
                            return result_type{std::move(v)};
                        }
                    }
                } else
#endif
                {
                    if constexpr (std::is_void_v<Result>) {
                        for (auto& t : tasks_) {
                            t.get_result_value();
                        }
                        return;
                    } else {
                        std::vector<in_variant_t<Result>> results;
                        results.reserve(tasks_.size());

                        for (auto& t : tasks_) {
                            results.push_back(for_variant(t.get_result_value()));
                        }

                        return results;
                    }
                }
            }

            std::coroutine_handle<> on_ready(std::size_t index, bool success)
            {
                size_t expected = tasks_.size();
                if (!success
                    && first_failed_task_index_.compare_exchange_strong(
                        expected, index, std::memory_order::acq_rel)
                    && stop_source_) {
                    stop_source_->request_stop();
                }
                if (counter_.fetch_sub(1) == 1) {
                    return std::move(continuation_);
                }
                return std::noop_coroutine();
            }
        };
        return awaiter{stop_source_, std::move(tasks_)};
    }

private:
    std::stop_source* stop_source_;
    std::vector<adaptor_task<Result>> tasks_;
};

} // namespace detail

template <awaitable... Awaitable>
auto when_all(Awaitable&&... awaitables)
{
    auto tasks = detail::make_adapter_tasks(std::forward<Awaitable>(awaitables)...);
    return detail::when_all_task(nullptr, std::move(tasks));
}

template <awaitable... Awaitable>
auto when_all(std::stop_source& stop_source, Awaitable&&... awaitables)
{
    auto tasks = detail::make_adapter_tasks(std::forward<Awaitable>(awaitables)...);
    return detail::when_all_task(&stop_source, std::move(tasks));
}

template <typename Range>
    requires std::ranges::range<Range> && awaitable<std::ranges::range_value_t<Range>>
auto when_all(Range&& awaitables)
{
    auto tasks = detail::make_adapter_tasks(std::forward<Range>(awaitables));
    return detail::when_all_task_range(nullptr, std::move(tasks));
}

template <typename Range>
    requires std::ranges::range<Range> && awaitable<std::ranges::range_value_t<Range>>
auto when_all(std::stop_source& stop_source, Range&& awaitables)
{
    auto tasks = detail::make_adapter_tasks(std::forward<Range>(awaitables));
    return detail::when_all_task_range(&stop_source, std::move(tasks));
}

} // namespace coral