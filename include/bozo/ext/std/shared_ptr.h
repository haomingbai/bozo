#pragma once

#include <bozo/type_traits.h>
#include <memory>

namespace bozo {
/**
 * @defgroup group-ext-std-shared_ptr std::shared_ptr
 * @ingroup group-ext-std
 * @brief [std::shared_ptr](https://en.cppreference.com/w/cpp/memory/shared_ptr) support
 *
 *@code
#include <bozo/ext/std/shared_ptr.h>
 *@endcode
 *
 * `std::shared_ptr<T>` is defined as #Nullable and uses the default implementation of `bozo::is_null()`.
 *
 * Function `bozo::allocate_nullable()` implementation is specialized via
 * [std::allocate_shared()](https://en.cppreference.com/w/cpp/memory/shared_ptr/allocate_shared).
 *
 * The `bozo::unwrap()` function is implemented via the dereference operator.
 */
///@{
template <typename T>
struct is_nullable<std::shared_ptr<T>> : std::true_type {};

template <typename T>
struct allocate_nullable_impl<std::shared_ptr<T>> {
    template <typename Alloc>
    static void apply(std::shared_ptr<T>& out, const Alloc& a) {
        out = std::allocate_shared<T, Alloc>(a);
    }
};

template <typename T>
struct unwrap_impl<std::shared_ptr<T>> : detail::functional::dereference {};
///@}
} // namespace bozo
