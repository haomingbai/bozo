#include "connection_mock.h"

#include <bozo/core/options.h>
#include <bozo/transaction.h>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace {

using namespace testing;
using namespace bozo::tests;

using bozo::error_code;
using bozo::time_traits;

struct async_end_transaction : Test {
    StrictMock<connection_gmock> connection {};
    StrictMock<executor_mock> callback_executor{};
    StrictMock<callback_gmock<connection_ptr<>>> callback {};
    StrictMock<executor_mock> strand {};
    io_context io;
    StrictMock<PGconn_mock> handle;
    connection_ptr<> conn = make_connection(connection, io, handle);
    decltype(bozo::make_options()) options = bozo::make_options();
    time_traits::duration timeout {42};
};

TEST_F(async_end_transaction, should_call_async_execute) {
    EXPECT_CALL(handle, PQstatus()).WillRepeatedly(Return(CONNECTION_OK));

    auto transaction = bozo::transaction(std::move(conn), options);

    const InSequence s;

    EXPECT_CALL(connection, async_execute()).WillOnce(Return());

    bozo::detail::async_end_transaction(std::move(transaction), empty_query {}, timeout, wrap(callback));
}

} // namespace
