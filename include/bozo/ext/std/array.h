#pragma once

#include <bozo/io/array.h>
#include <array>

namespace bozo {
/**
 * @defgroup group-ext-std-array std::array
 * @ingroup group-ext-std
 * @brief [std::array](https://en.cppreference.com/w/cpp/container/array) support
 *
 *@code
#include <bozo/ext/std/array.h>
 *@endcode
 *
 * `std::array<T, N>` is declared as an one dimensional array representation type
 * with fixed size.
 *
 * The `bozo::fit_array_size()` function throws `bozo::system_error` exception,
 * with `bozo::error::bad_array_size` error code, if it's size argument does not
 * equal to the array size.
 *
 * @models{Array}
 */
///@{
template <typename T, std::size_t size>
struct is_array<std::array<T, size>> : std::true_type {};

template <typename T, std::size_t S>
struct fit_array_size_impl<std::array<T, S>> {
    static void apply(const std::array<T, S>& array, size_type size) {
        if (size != static_cast<size_type>(array.size())) {
            throw system_error(error::bad_array_size,
                "received size " + std::to_string(size)
                + " does not match array size " + std::to_string(array.size()));
        }
    }
};
///@}
} // namespace bozo
