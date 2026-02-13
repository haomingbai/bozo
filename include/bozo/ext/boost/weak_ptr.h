#pragma once

#include <bozo/type_traits.h>
#include <boost/weak_ptr.hpp>

namespace bozo {
/**
 * @defgroup group-ext-boost-weak_ptr boost::weak_ptr
 * @ingroup group-ext-boost
 * @brief [boost::weak_ptr](https://www.boost.org/doc/libs/1_69_0/libs/smart_ptr/doc/html/smart_ptr.html#weak_ptr) support
 *
 *@code
#include <bozo/ext/boost/weak_ptr.h>
 *@endcode
 *
 * `boost::weak_ptr<T>` is defined as #Nullable and uses the implementation of `bozo::is_null()` examing the
 * result of `boost::weak_ptr::lock()` method call.
 *
 * The `bozo::unwrap()` function is implemented dereferencing the result of `boost::weak_ptr::lock()`
 * method call.
 * @note `bozo::unwrap()` from `boost::weak_ptr` is not safe for reference holding since no object owning.
 */
///@{
template <typename T>
struct is_nullable<boost::weak_ptr<T>> : std::true_type {};

template <typename T>
struct unwrap_impl<boost::weak_ptr<T>> {
    template <typename T1>
    constexpr static decltype(auto) apply(T1&& v) noexcept(noexcept(*v.lock())) {
        return *v.lock();
    }
};

template <typename T>
struct is_null_impl<boost::weak_ptr<T>> {
    constexpr static auto apply(const boost::weak_ptr<T>& v) noexcept(
            noexcept(bozo::is_null(v.lock()))) {
        return bozo::is_null(v.lock());
    }
};
///@}
} // namespace bozo
