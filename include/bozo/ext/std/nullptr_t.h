#pragma once

#include <bozo/pg/definitions.h>
#include <bozo/io/send.h>
#include <bozo/detail/functional.h>

namespace bozo {
/**
 * @defgroup group-ext-std-nullptr std::nullptr
 * @ingroup group-ext-std
 * @brief [std::nullptr](https://en.cppreference.com/w/cpp/utility/optional/nullptr) support
 *
 *@code
#include <bozo/ext/std/nullptr_t.h>
 *@endcode
 *
 * The `std::nullptr_t` type is defined as #Nullable which is always in null state.
 *
 * The `bozo::unwrap()` function with `std::nullptr_t` type argument returns std::nullptr.
 *
 * The `std::nullptr_t` type is mapped as `NULL` for PostgreSQL.
 */
///@{
template <>
struct is_nullable<std::nullptr_t> : std::true_type {};

template <>
struct unwrap_impl<std::nullptr_t> : detail::functional::forward {};

template <>
struct is_null_impl<std::nullptr_t> : detail::functional::always_true {};

template <>
struct send_impl<std::nullptr_t> {
    template <typename M, typename T>
    static ostream& apply(ostream& out, M&&, T&&) { return out;}
};
///@}
} // namespace bozo

BOZO_PG_BIND_TYPE(std::nullptr_t, "null")
