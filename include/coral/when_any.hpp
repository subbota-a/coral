#pragma once
#include <coral/async_result.hpp>
#include <coral/concepts.hpp>
#include <coral/detail/adapter_task.hpp>
#include <coral/detail/adaptor_task_helper.hpp>
#include <coral/detail/traits.hpp>
#include <coral/traits.hpp>

#include <atomic>
#include <coroutine>
#include <functional>
#include <stdexcept>
#include <stop_token>
#include <tuple>
#include <type_traits>
#include <utility>
#include <version>

namespace coral {
namespace detail {

template <typename... AdaptorTasks>
struct when_any_task {
    when_any_task(std::stop_source* stop_source, std::tuple<AdaptorTasks...>&& tasks)
        : stop_source_(stop_source)
        , tasks_(std::move(tasks))
    {
    }

    when_any_task(when_any_task&&) noexcept = default;
    when_any_task& operator=(when_any_task&&) noexcept = default;

    when_any_task(const when_any_task&) = delete;
    when_any_task& operator=(const when_any_task&) = delete;

    auto operator co_await() noexcept
    {
        class awaiter final {
        public:
            awaiter(std::stop_source* stop_source, std::tuple<AdaptorTasks...>&& tasks)
                : stop_source_(stop_source)
                , tasks_(std::move(tasks))
                , counter_{sizeof...(AdaptorTasks)}
                , first_completed_task_index_{sizeof...(AdaptorTasks)}
                , first_failed_task_index_{sizeof...(AdaptorTasks)}
            {
            }

            constexpr bool await_ready() const noexcept { return false; }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                continuation_ = continuation;

                constexpr auto task_count = sizeof...(AdaptorTasks);
                size_t started_tasks = 0;

                const auto start_task = [this, &started_tasks]<std::size_t Index>(
                                            std::integral_constant<std::size_t, Index>) {
                    std::get<Index>(tasks_).start(std::bind_front(&awaiter::on_ready, this, Index));
                    started_tasks = Index + 1;
                    if (first_completed_task_index_.load(std::memory_order::acquire) <= Index) {
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
                constexpr auto invalid_index = sizeof...(AdaptorTasks);

                const size_t completed_index =
                    this->first_completed_task_index_.load(std::memory_order::acquire);

#if CORAL_EXPECTED
                if constexpr (has_any_expected_v<typename AdaptorTasks::result_type...>) {
                    using value_type =
                        unique_variant<value_of<typename AdaptorTasks::result_type>...>;
                    using error_type =
                        non_void_unique_variant<error_of<typename AdaptorTasks::result_type>...>;
                    using expected_type = std::expected<value_type, error_type>;
                    using result_type = std::pair<size_t, expected_type>;

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
                    if (completed_index != invalid_index) {
                        return result_type{completed_index,
                            expected_type{std::in_place,
                                extract_task_value<value_type>(completed_index, this->tasks_)}};
                    }

                    const size_t failed_index =
                        this->first_failed_task_index_.load(std::memory_order::acquire);
                    return result_type{failed_index,
                        expected_type{std::unexpect,
                            extract_task_error<error_type>(failed_index, this->tasks_)}};
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

                } else
#endif
                {
                    using variant_type = unique_variant<typename AdaptorTasks::result_type...>;

                    if (completed_index == invalid_index) {
                        const size_t failed_index =
                            this->first_failed_task_index_.load(std::memory_order::acquire);
                        extract_task_value<variant_type>(failed_index, this->tasks_);
#if __cpp_lib_unreachable >= 202202L
                        std::unreachable();
#else
                        throw std::logic_error("exception expected");
#endif
                    }
                    return std::pair{completed_index,
                        extract_task_value<variant_type>(completed_index, this->tasks_)};
                }
            }

        private:
            std::stop_source* stop_source_;
            std::tuple<AdaptorTasks...> tasks_;
            std::atomic<size_t> counter_;
            std::atomic<std::size_t> first_completed_task_index_;
            std::atomic<std::size_t> first_failed_task_index_;
            std::coroutine_handle<> continuation_;

            std::coroutine_handle<> on_ready(std::size_t index, const bool success)
            {
                size_t expected = sizeof...(AdaptorTasks);

                if (success) {
                    if (first_completed_task_index_.compare_exchange_strong(
                            expected, index, std::memory_order::acq_rel)
                        && stop_source_) {
                        stop_source_->request_stop();
                    }
                } else {
                    first_failed_task_index_.compare_exchange_strong(
                        expected, index, std::memory_order::acq_rel);
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
struct when_any_task_range {
    when_any_task_range(std::stop_source* stop_source,
        std::vector<adaptor_task<Result>>&& tasks)
        : stop_source_(stop_source)
        , tasks_(std::move(tasks))
    {
    }

    when_any_task_range(when_any_task_range&&) noexcept = default;
    when_any_task_range& operator=(when_any_task_range&&) noexcept = default;

    when_any_task_range(const when_any_task_range&) = delete;
    when_any_task_range& operator=(const when_any_task_range&) = delete;

    auto operator co_await() noexcept
    {
        class awaiter final {
        public:
            awaiter(std::stop_source* stop_source, std::vector<adaptor_task<Result>>&& tasks)
                : stop_source_(stop_source)
                , tasks_(std::move(tasks))
                , counter_{tasks_.size()}
                , first_completed_task_index_{tasks_.size()}
                , first_failed_task_index_{tasks_.size()}
            {
            }

            bool await_ready() const noexcept { return tasks_.empty(); }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                continuation_ = continuation;

                for (size_t i = 0; i < tasks_.size() - 1; ++i) {
                    tasks_[i].start(std::bind_front(&awaiter::on_ready, this, i));
                    if (first_completed_task_index_.load(std::memory_order::acquire) <= i) {
                        const auto left_to_run = tasks_.size() - i - 1;
                        return counter_.fetch_sub(left_to_run, std::memory_order::release)
                                == left_to_run
                            ? continuation_
                            : std::noop_coroutine();
                    }
                }
                const auto last_index = tasks_.size() - 1;
                return tasks_[last_index].setup(
                    std::bind_front(&awaiter::on_ready, this, last_index));
            }

            auto await_resume()
            {
                const auto invalid_index = tasks_.size();
                if (invalid_index == 0) {
                    throw std::runtime_error("no tasks");
                }

                const size_t completed_index =
                    this->first_completed_task_index_.load(std::memory_order::acquire);

#if CORAL_EXPECTED
                if constexpr (is_expected_v<Result>) {
                    using result_type = std::pair<size_t, Result>;

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
                    if (completed_index == invalid_index) {
                        const size_t failed_index =
                            this->first_failed_task_index_.load(std::memory_order::acquire);

                        return result_type{failed_index, tasks_[failed_index].get_result_value()};
                    }
                    return result_type{completed_index, tasks_[completed_index].get_result_value()};

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
                } else
#endif
                {
                    if (completed_index == invalid_index) {
                        const size_t failed_index =
                            this->first_failed_task_index_.load(std::memory_order::acquire);
                        tasks_[failed_index].get_result_value();
#if __cpp_lib_unreachable >= 202202L
                        std::unreachable();
#else
                        throw std::logic_error("exception expected");
#endif
                    }
                    if constexpr (std::is_same_v<Result, void>) {
                        tasks_[completed_index].get_result_value();
                        return completed_index;
                    } else {
                        return std::pair<size_t, Result>{
                            completed_index, tasks_[completed_index].get_result_value()};
                    }
                }
            }

        private:
            std::stop_source* stop_source_;
            std::vector<adaptor_task<Result>> tasks_;
            std::atomic<size_t> counter_;
            std::atomic<std::size_t> first_completed_task_index_;
            std::atomic<std::size_t> first_failed_task_index_;
            std::coroutine_handle<> continuation_;

            std::coroutine_handle<> on_ready(std::size_t index, const bool success)
            {
                auto invalid_index = tasks_.size();

                if (success) {
                    if (first_completed_task_index_.compare_exchange_strong(
                            invalid_index, index, std::memory_order::acq_rel)
                        && stop_source_) {
                        stop_source_->request_stop();
                    }
                } else {
                    first_failed_task_index_.compare_exchange_strong(
                        invalid_index, index, std::memory_order::acq_rel);
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
auto when_any(Awaitable&&... awaitables)
{
    auto tasks = detail::make_adapter_tasks(std::forward<Awaitable>(awaitables)...);
    return detail::when_any_task(nullptr, std::move(tasks));
}

template <awaitable... Awaitable>
auto when_any(std::stop_source& stop_source, Awaitable&&... awaitables)
{
    auto tasks = detail::make_adapter_tasks(std::forward<Awaitable>(awaitables)...);
    return detail::when_any_task(&stop_source, std::move(tasks));
}

template <typename Range>
    requires std::ranges::range<Range> && awaitable<std::ranges::range_value_t<Range>>
auto when_any(Range&& awaitables)
{
    auto tasks = detail::make_adapter_tasks(std::forward<Range>(awaitables));
    return detail::when_any_task_range(nullptr, std::move(tasks));
}

template <typename Range>
    requires std::ranges::range<Range> && awaitable<std::ranges::range_value_t<Range>>
auto when_any(std::stop_source& stop_source, Range&& awaitables)
{
    auto tasks = detail::make_adapter_tasks(std::forward<Range>(awaitables));
    return detail::when_any_task_range(&stop_source, std::move(tasks));
}

} // namespace coral