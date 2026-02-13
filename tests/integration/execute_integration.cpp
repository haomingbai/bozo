#include <bozo/connection_info.h>
#include <bozo/query_builder.h>
#include <bozo/execute.h>

#include <gtest/gtest.h>

namespace {

using namespace testing;

TEST(execute, should_perform_operation_without_result) {
    using namespace bozo::literals;

    bozo::io_context io;
    bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);

    bozo::execute(conn_info[io], "BEGIN"_SQL,
        [&](bozo::error_code ec, auto conn) {
            ASSERT_FALSE(ec) << ec.message() << " | " << error_message(conn) << " | " << get_error_context(conn);
            EXPECT_FALSE(bozo::connection_bad(conn));
        });

    io.run();
}

} // namespace
