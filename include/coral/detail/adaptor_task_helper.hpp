#pragma once
#include <coral/detail/traits.hpp>

#include <string>

namespace coral::detail {

template <typename VariantType, typename TaskTuple, std::size_t Index>
VariantType extract_task_value_at_index(TaskTuple& tasks)
{
    using task_type = std::tuple_element_t<Index, std::remove_reference_t<TaskTuple>>;
    using result_type = typename task_type::result_type;

    if constexpr (std::is_void_v<result_type>) {
        std::get<Index>(tasks).get_result_value();
        return {for_variant()};
    } else {
        return {for_variant(extract_value(std::get<Index>(tasks).get_result_value()))};
    }
}

template <typename VariantType, typename TaskTuple, std::size_t... Is>
VariantType
extract_task_value_impl(const size_t index, TaskTuple& tasks, std::index_sequence<Is...>)
{
    // Array of function pointers for each index
    using extract_fn_t = VariantType (*)(TaskTuple&);
    constexpr extract_fn_t extractors[] = {
        &extract_task_value_at_index<VariantType, TaskTuple, Is>...};

    return extractors[index](tasks);
}

template <typename VariantType, typename TaskTuple>
VariantType extract_task_value(const size_t index, TaskTuple& tasks)
{
    return extract_task_value_impl<VariantType>(
        index, tasks, std::make_index_sequence<std::tuple_size_v<TaskTuple>>{});
}

template <typename... AdaptorTasks>
using tuple_of_adaptor_task_result_t =
    std::tuple<in_tuple_t<typename AdaptorTasks::result_type>...>;

#if CORAL_EXPECTED

template <typename VariantType, typename TaskTuple, std::size_t Index>
VariantType extract_task_error_at_index(TaskTuple& tasks)
{
    using element_type = decltype(std::get<Index>(tasks).get_result_value());

    if constexpr (is_expected_v<element_type>) {
        return {for_variant(extract_error(std::get<Index>(tasks).get_result_value()))};
    }

    (void)std::get<Index>(tasks).get_result_value();
#if __cpp_lib_unreachable >= 202202L
    std::unreachable();
#else
    throw std::logic_error("exception expected");
#endif
}

template <typename VariantType, typename TaskTuple, std::size_t... Is>
VariantType
extract_task_error_impl(const size_t index, TaskTuple& tasks, std::index_sequence<Is...>)
{
    // Array of function pointers for each index
    using extract_fn_t = VariantType (*)(TaskTuple&);
    constexpr extract_fn_t extractors[] = {
        &extract_task_error_at_index<VariantType, TaskTuple, Is>...};

    return extractors[index](tasks);
}

template <typename VariantType, typename TaskTuple>
VariantType extract_task_error(const size_t index, TaskTuple& tasks)
{
    return extract_task_error_impl<VariantType>(
        index, tasks, std::make_index_sequence<std::tuple_size_v<TaskTuple>>{});
}

#endif

} // namespace coral::detail