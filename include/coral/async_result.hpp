#pragma once
#include <exception>
#include <functional>
#include <type_traits>
#include <variant>
namespace coral {

template <typename T>
class async_result {
public:
    template <typename V>
    explicit async_result(V&& value) noexcept
        : result_(std::forward<V>(value)) {}

    [[nodiscard]] bool has_value() noexcept { return result_.index() == 0; }

    const T& value() const& {
        if (result_.index() == 0) {
            return std::get<0>(result_);
        }
        std::rethrow_exception(std::get<1>(result_));
    }
    std::remove_reference_t<T>& value() & {
        if (result_.index() == 0) {
            return std::get<0>(result_);
        }
        std::rethrow_exception(std::get<1>(result_));
    }
    T&& value() && {
        if (result_.index() == 0) {
            return static_cast<T&&>(std::get<0>(result_));
        }
        std::rethrow_exception(std::get<1>(result_));
    }
    std::exception_ptr error() const { return std::get<1>(result_); }

private:
    using stored_type = std::conditional_t<std::is_reference_v<T>,
        std::reference_wrapper<std::remove_reference_t<T>>,
        T>;
    std::variant<stored_type, std::exception_ptr> result_;
};

template <>
class async_result<void> {
public:
    explicit async_result() {}
    explicit async_result(std::exception_ptr error) noexcept
        : result_(error) {}

    [[nodiscard]] bool has_value() noexcept { return result_.index() == 0; }

    void value() {
        if (result_.index() == 0) {
            return;
        }
        std::rethrow_exception(std::get<1>(result_));
    }
    std::exception_ptr error() const { return std::get<1>(result_); }

private:
    std::variant<std::monostate, std::exception_ptr> result_;
};

} // namespace coral