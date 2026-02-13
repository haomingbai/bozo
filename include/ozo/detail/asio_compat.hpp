#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>

#include <utility>

namespace ozo::detail::asio_compat {

namespace asio = boost::asio;

using io_context = asio::io_context;
using any_io_executor = asio::any_io_executor;

template <typename Executor>
auto make_strand_executor(const Executor& ex) {
    return asio::make_strand(ex);
}

} // namespace ozo::detail::asio_compat
