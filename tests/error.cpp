#include <bozo/error.h>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace {

TEST(connection_error, should_match_to_mapped_errors_only) {
    const auto connection_error = bozo::error_condition{bozo::errc::connection_error};
    EXPECT_EQ(connection_error, bozo::sqlstate::make_error_code(bozo::sqlstate::connection_does_not_exist));
    EXPECT_EQ(connection_error, boost::asio::error::connection_aborted);
    EXPECT_EQ(connection_error, boost::system::errc::make_error_code(boost::system::errc::io_error));
    EXPECT_EQ(connection_error, bozo::error::pq_socket_failed);
    EXPECT_NE(connection_error, bozo::error::bad_object_size);
}

TEST(database_readonly, should_match_to_mapped_errors_only) {
    const auto database_readonly = bozo::error_condition{bozo::errc::database_readonly};
    EXPECT_EQ(database_readonly, bozo::sqlstate::make_error_code(bozo::sqlstate::read_only_sql_transaction));
    EXPECT_NE(database_readonly, bozo::error::pq_socket_failed);
}

TEST(introspection_error, should_match_to_mapped_errors_only) {
    const auto introspection_error = bozo::error_condition{bozo::errc::introspection_error};
    EXPECT_EQ(introspection_error, bozo::error::bad_object_size);
    EXPECT_NE(introspection_error, bozo::error::pq_socket_failed);
}

TEST(type_mismatch, should_match_to_mapped_errors_only) {
    const auto type_mismatch = bozo::error_condition{bozo::errc::type_mismatch};
    EXPECT_EQ(type_mismatch, bozo::error::oid_type_mismatch);
    EXPECT_NE(type_mismatch, bozo::error::pq_socket_failed);
}

TEST(protocol_error, should_match_to_mapped_errors_only) {
    const auto protocol_error = bozo::error_condition{bozo::errc::protocol_error};
    EXPECT_EQ(protocol_error, bozo::error::no_sql_state_found);
    EXPECT_NE(protocol_error, bozo::error::pq_socket_failed);
}

}
