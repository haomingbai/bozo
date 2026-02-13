#pragma once

#include <bozo/core/none.h>
#include <bozo/impl/async_request.h>

namespace bozo::impl {

template <typename P, typename Q, typename TimeConstraint, typename Handler>
inline void async_execute(P&& provider, Q&& query, TimeConstraint t, Handler&& handler) {
    static_assert(ConnectionProvider<P>, "is not a ConnectionProvider");
    static_assert(BinaryQueryConvertible<Q>, "query should be convertible to the binary_query");
    static_assert(bozo::TimeConstraint<TimeConstraint>, "should model TimeConstraint concept");
    async_get_connection(std::forward<P>(provider), deadline(t),
        async_request_op {
            std::forward<Q>(query),
            deadline(t),
            none,
            std::forward<Handler>(handler)
        }
    );
}

} // namespace bozo::impl
