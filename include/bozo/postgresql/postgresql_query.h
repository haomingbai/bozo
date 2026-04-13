/**
 * @file postgresql_query.h
 * @brief bozo-owned parameterized PostgreSQL query helpers.
 * @author Haoming Bai <haomingbai@hotmail.com>
 * @date   2026-04-13
 *
 * Copyright (c) 2026 Haoming Bai
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifndef BOZO_POSTGRESQL_POSTGRESQL_QUERY_H_
#define BOZO_POSTGRESQL_POSTGRESQL_QUERY_H_

#include <string_view>
#include <utility>

#include <boost/hana/tuple.hpp>
#include <boost/hana/core/when.hpp>

#include "ozo/io/binary_query.h"
#include "ozo/query.h"

namespace bozo::postgresql {

/**
 * @brief bozo-owned parameterized query representation.
 *
 * Unlike OZO's generic `Query` path, this type is adapted directly to
 * `ozo::binary_query`, so move-only parameters remain supported.
 */
template <class Text, class... Params>
class ParameterizedQuery {
public:
  using text_type = Text;
  using params_type = boost::hana::tuple<Params...>;

  constexpr ParameterizedQuery(Text text, Params... params)
      : text_(std::move(text)),
        params_(boost::hana::make_tuple(std::move(params)...)) {}

  [[nodiscard]] constexpr const text_type& text() const noexcept {
    return text_;
  }

  [[nodiscard]] constexpr const params_type& params() const noexcept {
    return params_;
  }

private:
  text_type text_;
  params_type params_;
};

template <class Text, class... Params>
ParameterizedQuery(Text, Params...)
    -> ParameterizedQuery<std::decay_t<Text>, std::decay_t<Params>...>;

template <class Text, class... Params>
[[nodiscard]] constexpr auto MakeQuery(Text&& text, Params&&... params) {
  static_assert(ozo::QueryText<std::decay_t<Text>>,
                "text must model ozo::QueryText");
  return ParameterizedQuery<std::decay_t<Text>, std::decay_t<Params>...>(
      std::forward<Text>(text), std::forward<Params>(params)...);
}

template <class... Params>
[[nodiscard]] constexpr auto MakeQuery(const char* text, Params&&... params) {
  return MakeQuery(std::string_view(text), std::forward<Params>(params)...);
}

}  // namespace bozo::postgresql

namespace ozo {

template <class Text, class... Params>
struct to_binary_query_impl<
    bozo::postgresql::ParameterizedQuery<Text, Params...>,
    boost::hana::when<true>> {
  template <typename OidMap, typename Allocator>
  static binary_query apply(
      const bozo::postgresql::ParameterizedQuery<Text, Params...>& query,
      const OidMap& oid_map, const Allocator& allocator) {
    return binary_query(query.text(), query.params(), oid_map, allocator);
  }
};

}  // namespace ozo

#endif  // BOZO_POSTGRESQL_POSTGRESQL_QUERY_H_
