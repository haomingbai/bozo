#pragma once

#ifndef BOZO_TESTS_SUPPORT_POSTGRESQL_TEST_SUPPORT_H_
#define BOZO_TESTS_SUPPORT_POSTGRESQL_TEST_SUPPORT_H_

#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>

#include <cstdlib>
#include <future>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "bozo/bozo.h"

namespace bozo::test {

inline std::string GetTestConninfo() {
  if (const char *value = std::getenv("BOZO_PG_TEST_CONNINFO");
      value != nullptr) {
    return value;
  }
  if (const char *value = std::getenv("OZO_PG_TEST_CONNINFO");
      value != nullptr) {
    return value;
  }
  return {};
}

inline std::string RequireTestConninfo() { return GetTestConninfo(); }

inline std::string MakeInvalidConninfo() {
  return "host=127.0.0.1 port=1 dbname=bozo_test user=bozo password=bozo "
         "connect_timeout=1";
}

template <typename StartFn>
inline bozo::postgresql::PostgreSqlTaskResult
RunTaskOperation(boost::asio::io_context &io, StartFn &&start) {
  std::promise<bozo::postgresql::PostgreSqlTaskResult> promise;
  auto future = promise.get_future();
  auto ec = std::forward<StartFn>(start)(
      [&promise](const bozo::postgresql::PostgreSqlTaskResult &result) mutable {
        promise.set_value(result);
      });
  if (ec) {
    throw std::system_error(ec);
  }
  io.run();
  io.restart();
  return future.get();
}

} // namespace bozo::test

#endif // BOZO_TESTS_SUPPORT_POSTGRESQL_TEST_SUPPORT_H_
