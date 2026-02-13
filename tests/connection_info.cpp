#include <bozo/connection_info.h>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace {

TEST(connection_info, should_return_error_and_bad_connect_for_invalid_connection_info) {
    bozo::io_context io;
    bozo::connection_info conn_info("invalid connection info");

    bozo::get_connection(conn_info[io], [](bozo::error_code ec, auto conn){
        EXPECT_TRUE(ec);
        EXPECT_TRUE(!bozo::error_message(conn).empty());
        EXPECT_TRUE(bozo::connection_bad(conn));
    });

    io.run();
}

TEST(make_connection_info, should_not_throw) {
    EXPECT_NO_THROW(bozo::make_connection_info("conn info string"));
}

} // namespace
