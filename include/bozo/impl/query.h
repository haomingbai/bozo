#pragma once

#include <bozo/type_traits.h>

#include <boost/hana/tuple.hpp>

#include <string_view>

namespace bozo::impl {

template <class Text, class ... ParamsT>
struct query {
    Text text;
    hana::tuple<ParamsT ...> params;
};

} // namespace bozo::impl

namespace bozo {
template <class ...Ts>
struct get_query_text_impl<impl::query<Ts...>> {
    static constexpr decltype(auto) apply(const impl::query<Ts...>& q) noexcept {
        return q.text;
    }
};

template <typename ...Ts>
struct get_query_params_impl<impl::query<Ts...>> {
    static constexpr decltype(auto) apply(const impl::query<Ts...>& q) noexcept {
        return q.params;
    }
};

template <class Text, class ...ParamsT>
inline constexpr auto make_query(Text&& text, ParamsT&& ...params) {
    static_assert(QueryText<Text>, "text must be QueryText concept");
    using query = impl::query<std::decay_t<Text>, std::decay_t<ParamsT>...>;
    return query {std::forward<Text>(text), hana::make_tuple(std::forward<ParamsT>(params)...)};
}

} // namespace bozo
