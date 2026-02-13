#pragma once

#include <bozo/pg/definitions.h>
#include <utility>
#include <boost/hana/ext/std/pair.hpp>

/**
 * @defgroup group-ext-std-pair std::pair
 * @ingroup group-ext-std
 * @brief [std::pair](https://en.cppreference.com/w/cpp/utility/pair) support
 *
 *@code
#include <bozo/ext/std/pair.h>
 *@endcode
 *
 * `std::pair<T1, T2>` is defined as a generic composite type of two elements
 * and mapped as PostgreSQL `Record` type and its array.
 */
namespace bozo::definitions {

template <typename ...Ts>
struct type<std::pair<Ts...>> : pg::type_definition<decltype("record"_s)>{};

template <typename ...Ts>
struct array<std::pair<Ts...>> : pg::array_definition<decltype("record"_s)>{};

} // namespace bozo::definitions

namespace bozo {

template <typename ...Ts>
struct is_composite<std::pair<Ts...>> : std::true_type {};

} // namespace bozo
