#pragma once

#include <bozo/type_traits.h>
#include <boost/scoped_ptr.hpp>

namespace bozo {
/**
 * @defgroup group-ext-boost-scoped_ptr boost::scoped_ptr
 * @ingroup group-ext-boost
 * @brief [boost::scoped_ptr](https://www.boost.org/doc/libs/1_69_0/libs/smart_ptr/doc/html/smart_ptr.html#scoped_ptr) support
 *
 *@code
#include <bozo/ext/boost/scoped_ptr.h>
 *@endcode
 *
 * `boost::scoped_ptr<T>` is defined as #Nullable and uses the default implementation of `bozo::is_null()`.
 *
 * Function `bozo::allocate_nullable()` implementation is specialized via direct call of `operator new`, so the allocator
 * argument is ignored.
 *
 * The `bozo::unwrap()` function is implemented via the dereference operator.
 */
///@{
template <typename T>
struct is_nullable<boost::scoped_ptr<T>> : std::true_type {};

template <typename T>
struct allocate_nullable_impl<boost::scoped_ptr<T>> {
    template <typename Alloc>
    static void apply(boost::scoped_ptr<T>& out, const Alloc&) {
        out.reset(new T{});
    }
};

template <typename T>
struct unwrap_impl<boost::scoped_ptr<T>> : detail::functional::dereference {};
///@}
} // namespace bozo
