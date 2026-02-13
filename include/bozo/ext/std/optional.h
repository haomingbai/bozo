#pragma once

#include <bozo/type_traits.h>
#include <bozo/optional.h>

namespace bozo {
/**
 * @defgroup group-ext-std-optional std::optional
 * @ingroup group-ext-std
 * @brief [std::optional](https://en.cppreference.com/w/cpp/utility/optional) type support
 *
 *@code
#include <bozo/ext/std/optional.h>
 *@endcode
 *
 * The `std::optional<T>` type is defined as #Nullable and uses default
 * implementation of related functions.
 *
 * The `bozo::unwrap()` function is implemented via the dereference operator.
 */
///@{
#ifdef BOZO_STD_OPTIONAL
template <typename T>
struct is_nullable<BOZO_STD_OPTIONAL<T>> : std::true_type {};

template <typename T>
struct unwrap_impl<BOZO_STD_OPTIONAL<T>> : detail::functional::dereference {};
#endif
///@}
} // namespace bozo
