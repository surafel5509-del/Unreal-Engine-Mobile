#pragma once

// Nova Engine — Result<T, Error>.
// The engine is built with -fno-exceptions: fallible operations return a
// Result instead of throwing. Usage:
//
//   nova::Result<std::string, EngineError> loadName() {
//       if (!ok) return nova::Result<...>::failure(EngineError::NotFound);
//       return nova::Result<...>::success(std::move(name));
//   }
//
// Result<T, E> and Result<void, E> are [[nodiscard]]: ignoring a fallible
// call is a compile error.

#include "core/Types.h"

#include <variant>

namespace nova {

template <typename T, typename E>
class [[nodiscard]] Result {
public:
    using value_type = T;
    using error_type = E;

    static Result success(T value) { return Result(std::move(value), false); }
    static Result failure(E error) { return Result(std::move(error), true); }

    bool isOk() const noexcept { return m_data.index() == 0; }
    bool isError() const noexcept { return m_data.index() == 1; }
    explicit operator bool() const noexcept { return isOk(); }

    T& value() & { return std::get<0>(m_data); }
    const T& value() const& { return std::get<0>(m_data); }
    T&& value() && { return std::get<0>(std::move(m_data)); }

    E& error() & { return std::get<1>(m_data); }
    const E& error() const& { return std::get<1>(m_data); }

    /// Value if OK, otherwise the provided fallback (copy).
    T valueOr(T fallback) const { return isOk() ? std::get<0>(m_data) : std::move(fallback); }

private:
    Result(T value, bool /*errorTag*/) : m_data(std::in_place_index<0>, std::move(value)) {}
    Result(E error, bool /*errorTag*/) : m_data(std::in_place_index<1>, std::move(error)) {}

    std::variant<T, E> m_data;
};

template <typename E>
class [[nodiscard]] Result<void, E> {
public:
    using value_type = void;
    using error_type = E;

    static Result success() { return Result(true); }
    static Result failure(E error) { return Result(std::move(error)); }

    bool isOk() const noexcept { return m_ok; }
    bool isError() const noexcept { return !m_ok; }
    explicit operator bool() const noexcept { return m_ok; }

    const E& error() const { return std::get<E>(m_data); }
    E& error() { return std::get<E>(m_data); }

private:
    explicit Result(bool ok) : m_ok(ok), m_data(std::monostate{}) {}
    explicit Result(E error) : m_ok(false), m_data(std::move(error)) {}

    bool m_ok;
    std::variant<std::monostate, E> m_data;
};

}  // namespace nova
