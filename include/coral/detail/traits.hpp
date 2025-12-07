#pragma once
#include <coral/detail/config.hpp>

#include <format>
#include <functional>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#if CORAL_EXPECTED
#include <expected>
#endif

namespace coral::detail {

template <typename T>
struct in_variant {
    using type = T;
};

template <>
struct in_variant<void> {
    using type = std::monostate;
};

template <typename T>
struct in_variant<T&> {
    using type = std::reference_wrapper<T>;
};

template <typename T>
struct in_variant<T&&> {
    using type = T;
};

template <typename T>
using in_variant_t = typename in_variant<T>::type;

template <typename T>
constexpr decltype(auto) for_variant(T&& v)
{
    return std::forward<T>(v);
}

template <typename T>
constexpr auto for_variant(T& v)
{
    return std::ref(v);
}

constexpr std::monostate for_variant() { return {}; }

template <typename T>
struct in_tuple {
    using type = T;
};

template <>
struct in_tuple<void> {
    using type = std::monostate;
};

template <typename T>
struct in_tuple<T&&> {
    using type = T;
};

template <typename T>
using in_tuple_t = typename in_tuple<T>::type;

template <typename T>
constexpr decltype(auto) for_tuple(T&& v)
{
    return std::forward<T>(v);
}

std::monostate for_tuple() { return {}; }

// Check if type T is contained in the list Ts...
template <typename T, typename... Ts>
struct contains : std::false_type {};

template <typename T, typename U, typename... Ts>
struct contains<T, U, Ts...>
    : std::conditional_t<std::is_same_v<T, U>, std::true_type, contains<T, Ts...>> {};

template <typename T, typename... Ts>
inline constexpr bool contains_v = contains<T, Ts...>::value;

// Get unique types from a type list (keeps first occurrence)
template <typename Result, typename... Ts>
struct unique_types_impl;

template <typename... Result>
struct unique_types_impl<std::tuple<Result...>> {
    using type = std::tuple<Result...>;
};

template <typename... Result, typename T, typename... Ts>
struct unique_types_impl<std::tuple<Result...>, T, Ts...> {
    using type = std::conditional_t<contains_v<T, Result...>,
        typename unique_types_impl<std::tuple<Result...>, Ts...>::type, // T already in result, skip
                                                                        // it
        typename unique_types_impl<std::tuple<Result..., T>, Ts...>::type // Add T to result
        >;
};

template <typename... Ts>
using unique_types = typename unique_types_impl<std::tuple<>, Ts...>::type;

template <typename Result, typename... Ts>
struct non_void_unique_types_impl;

template <typename... Result>
struct non_void_unique_types_impl<std::tuple<Result...>> {
    using type = std::tuple<Result...>;
};

template <typename... Result, typename T, typename... Ts>
struct non_void_unique_types_impl<std::tuple<Result...>, T, Ts...> {
    using type = std::conditional_t<std::is_void_v<T>,
        typename non_void_unique_types_impl<std::tuple<Result...>, Ts...>::type, // Skip void
        std::conditional_t<contains_v<in_variant_t<T>, Result...>,
            typename non_void_unique_types_impl<std::tuple<Result...>, Ts...>::type, // Already in
                                                                                     // result, skip
            typename non_void_unique_types_impl<std::tuple<Result..., in_variant_t<T>>,
                Ts...>::type // Add T to result
            >>;
};

template <typename... Ts>
using non_void_unique_types = typename non_void_unique_types_impl<std::tuple<>, Ts...>::type;

// ============================================================================
// Tuple to variant conversion
// ============================================================================

template <typename Tuple>
struct tuple_to_variant;

template <typename... Ts>
struct tuple_to_variant<std::tuple<Ts...>> {
    using type = std::variant<Ts...>;
};

template <typename Tuple>
using tuple_to_variant_t = typename tuple_to_variant<Tuple>::type;

template <typename... Ts>
using unique_variant = tuple_to_variant_t<unique_types<in_variant_t<Ts>...>>;

template <typename... Ts>
using non_void_unique_variant = tuple_to_variant_t<non_void_unique_types<Ts...>>;

template <typename T>
struct is_expected : std::false_type {};

template <typename T>
inline constexpr bool is_expected_v = is_expected<std::remove_cvref_t<T>>::value;

template <typename... T>
struct has_any_expected_impl {
    static constexpr bool value = (is_expected_v<T> || ...);
};

template <typename... T>
inline constexpr bool has_any_expected_v = has_any_expected_impl<T...>::value;

template <typename Tuple, typename = std::make_index_sequence<std::tuple_size_v<Tuple>>>
struct has_any_expected_in_tuple_impl;

template <typename Tuple, std::size_t... Is>
struct has_any_expected_in_tuple_impl<Tuple, std::index_sequence<Is...>> {
    static constexpr bool value = (is_expected_v<std::tuple_element_t<Is, Tuple>> || ...);
};

template <typename Tuple>
inline constexpr bool has_any_expected_in_tuple_v = has_any_expected_in_tuple_impl<Tuple>::value;

template <typename T>
struct value_of_impl {
    using value_type = T;
    using error_type = void;
};

template <typename T>
using value_of = value_of_impl<T>::value_type;

template <typename... Args>
struct tuple_of_values_impl;

template <typename... Args>
struct tuple_of_values_impl<std::tuple<Args...>> {
    using type = std::tuple<in_tuple_t<value_of<Args>>...>;
};

template <typename Tuple>
using tuple_of_values_t = typename tuple_of_values_impl<std::remove_cvref_t<Tuple>>::type;

static_assert(std::is_same_v<value_of<int>, int>);

template <typename T>
using error_of = value_of_impl<std::remove_cvref_t<T>>::error_type;

template <typename... Args>
struct variant_of_errors_impl;

template <typename... Args>
struct variant_of_errors_impl<std::tuple<Args...>> {
    using type = non_void_unique_variant<error_of<Args>...>;
};

template <typename Tuple>
using variant_of_errors_t = typename variant_of_errors_impl<std::remove_cvref_t<Tuple>>::type;

template <typename T>
constexpr decltype(auto) extract_value(T&& v)
{
    return std::forward<T>(v);
}

#if CORAL_EXPECTED
template <typename V, typename E>
decltype(auto) extract_value(std::expected<V, E>& v)
{
    return std::move(*v);
}

template <typename V, typename E>
decltype(auto) extract_value(std::expected<V, E>&& v)
{
    return std::move(*v);
}

template <typename E>
std::monostate extract_value(std::expected<void, E>&)
{
    return {};
}

template <typename E>
std::monostate extract_value(std::expected<void, E>&&)
{
    return {};
}
#endif

template <typename Tuple>
auto extract_values_for_tuple(Tuple&& tuple) -> tuple_of_values_t<Tuple>
{
    return std::apply(
        [](auto&&... items) -> tuple_of_values_t<Tuple> {
            return {for_tuple(extract_value(std::forward<decltype(items)>(items)))...};
        },
        std::forward<Tuple>(tuple));
}

#if CORAL_EXPECTED

template <typename T, typename E>
struct is_expected<std::expected<T, E>> : std::true_type {};

template <typename V, typename E>
struct value_of_impl<std::expected<V, E>> {
    using value_type = V;
    using error_type = E;
};

static_assert(std::is_same_v<value_of<std::expected<int, long>>, int>);

template <typename Tuple>
auto make_expected_of_values(Tuple&& tuple)
    -> std::expected<tuple_of_values_t<Tuple>, variant_of_errors_t<Tuple>>
{
    return {extract_values_for_tuple(tuple)};
}

template <typename VariantResult, std::size_t Index, typename Tuple>
VariantResult extract_error_at_index(Tuple&& result)
{
    using element_type = std::tuple_element_t<Index, std::remove_reference_t<Tuple>>;

    if constexpr (is_expected_v<element_type>) {
        return for_variant<VariantResult>(std::get<Index>(std::forward<Tuple>(result)).error());
    }

    throw std::invalid_argument(std::format("{} arguments is not expected", Index));
}

template <typename VariantResult, typename Tuple, std::size_t... Is>
VariantResult
extract_error_from_tuple(const size_t index, Tuple&& results, std::index_sequence<Is...>)
{
    // Array of function pointers for each index
    using extract_fn_t = VariantResult (*)(Tuple&&);
    constexpr extract_fn_t extractors[] = {&extract_error_at_index<VariantResult, Is, Tuple>...};

    return extractors[index](std::forward<Tuple>(results));
}

template <typename Tuple>
auto make_expected_of_error_at_index(const size_t index, Tuple&& tuple)
    -> std::expected<tuple_of_values_t<Tuple>, variant_of_errors_t<Tuple>>
{
    return std::unexpected(extract_error_from_tuple<variant_of_errors_t<Tuple>>(index,
        std::forward<Tuple>(tuple),
        std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<Tuple>>>{}));
}

template <typename V, typename E>
decltype(auto) extract_error(std::expected<V, E>& v)
{
    return std::move(v.error());
}

template <typename V, typename E>
decltype(auto) extract_error(std::expected<V, E>&& v)
{
    return std::move(v.error());
}

#endif

} // namespace coral::detail