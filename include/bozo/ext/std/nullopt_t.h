#pragma once

#include <bozo/pg/definitions.h>
#include <bozo/optional.h>
#include <bozo/io/send.h>
#include <bozo/detail/functional.h>

namespace bozo {
/**
 * @defgroup group-ext-std-nullopt std::nullopt_t
 * @ingroup group-ext-std
 * @brief [std::nullopt](https://en.cppreference.com/w/cpp/utility/optional/nullopt) support
 *
 *@code
#include <bozo/ext/std/nullopt_t.h>
 *@endcode
 *
 * The `std::nullopt_t` type is defined as #Nullable which is always in null state.
 *
 * The `bozo::unwrap()` function with `std::nullopt_t` type argument returns `std::nullopt`.
 *
 * The `std::nullopt_t` type is mapped as `NULL` for PostgreSQL.
 */
///@{
template <>
struct is_nullable<BOZO_NULLOPT_T> : std::true_type {};

template <>
struct unwrap_impl<BOZO_NULLOPT_T> : detail::functional::forward {};

template <>
struct is_null_impl<BOZO_NULLOPT_T> : detail::functional::always_true {};

template <>
struct send_impl<BOZO_NULLOPT_T> {
    template <typename M, typename T>
    static ostream& apply(ostream& out, M&&, T&&) { return out;}
};
///@}
} // namespace bozo

BOZO_PG_BIND_TYPE(BOZO_NULLOPT_T, "null")
