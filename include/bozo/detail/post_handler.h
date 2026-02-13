#pragma once

#include <bozo/asio.h>
#include <bozo/error.h>
#include <bozo/connection.h>

#include <boost/asio/post.hpp>

namespace bozo::detail {

template <typename Handler>
struct post_handler {
    Handler handler;

    post_handler(Handler handler) : handler(std::move(handler)) {}

    template <typename Connection>
    void operator() (error_code ec, Connection&& connection) {
        auto ex = get_executor(connection);
        asio::post(ex, detail::bind(std::move(handler), std::move(ec), std::forward<Connection>(connection)));
    }
};

} // namespace bozo::detail
