#include <bozo/transaction_status.h>
#include "connection_mock.h"
#include <boost/asio/spawn.hpp>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace {

using namespace testing;

struct get_transaction_status : Test {
    bozo::tests::io_context io;
    StrictMock<bozo::tests::PGconn_mock> handle;
    auto make_connection() {
        using namespace bozo::tests;
        EXPECT_CALL(handle, PQstatus()).WillRepeatedly(Return(CONNECTION_OK));
        return std::make_shared<connection<>>(connection<>{
            std::addressof(handle),
            {}, nullptr, "", nullptr
        });
    }
};

TEST_F(get_transaction_status, should_return_transaction_status_unknown_for_null_transaction) {
    const auto conn = bozo::tests::connection_ptr<>();
    EXPECT_EQ(bozo::transaction_status::unknown, bozo::get_transaction_status(conn));
}

TEST_F(get_transaction_status, should_return_throw_for_unsupported_status) {
    const auto conn = make_connection();
    EXPECT_CALL(handle, PQtransactionStatus())
        .WillOnce(Return(static_cast<PGTransactionStatusType>(-1)));
    EXPECT_THROW(bozo::get_transaction_status(conn), std::invalid_argument);
}

namespace with_params {

struct get_transaction_status : ::get_transaction_status,
        WithParamInterface<std::tuple<PGTransactionStatusType, bozo::transaction_status>> {
};

TEST_P(get_transaction_status, should_return_status_for_connection){
    const auto conn = make_connection();
    EXPECT_CALL(handle, PQtransactionStatus())
        .WillOnce(Return(std::get<0>(GetParam())));
    EXPECT_EQ(std::get<1>(GetParam()), bozo::get_transaction_status(conn));
}

INSTANTIATE_TEST_SUITE_P(
    with_any_PGTransactionStatusType,
    get_transaction_status,
    testing::Values(
        std::make_tuple(PQTRANS_UNKNOWN, bozo::transaction_status::unknown),
        std::make_tuple(PQTRANS_IDLE, bozo::transaction_status::idle),
        std::make_tuple(PQTRANS_ACTIVE, bozo::transaction_status::active),
        std::make_tuple(PQTRANS_INTRANS, bozo::transaction_status::transaction),
        std::make_tuple(PQTRANS_INERROR, bozo::transaction_status::error)
    )
);

} // namespace with_params

}
