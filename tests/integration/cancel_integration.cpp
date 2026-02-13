#include <bozo/connection_info.h>
#include <bozo/cancel.h>
#include <bozo/execute.h>
#include <bozo/shortcuts.h>

#include <boost/asio/spawn.hpp>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#define ASSERT_REQUEST_OK(ec, conn)\
    ASSERT_FALSE(ec) << ec.message() \
        << "|" << bozo::error_message(conn) \
        << "|" << bozo::get_error_context(conn) << std::endl

namespace {

namespace hana = boost::hana;

using namespace testing;

TEST(cancel, should_cancel_operation) {
    using namespace bozo::literals;
    using namespace std::chrono_literals;
    using namespace hana::literals;

    bozo::io_context io;
    boost::asio::steady_timer timer(io);

    boost::asio::spawn(io, [&io, &timer](auto yield){
        const bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);
        bozo::error_code ec;
        auto conn = bozo::get_connection(conn_info[io], yield[ec]);
        EXPECT_FALSE(ec);
        boost::asio::spawn(yield, [&io, &timer, handle = get_cancel_handle(conn)](auto yield) mutable {
            timer.expires_after(1s);
            bozo::error_code ec;
            timer.async_wait(yield[ec]);
            if (!ec) {
                // Guard is needed since cancel will be served with external
                // system executor, so we need to preserve our io_context from
                // stop until all the operation processed properly
                auto guard = boost::asio::make_work_guard(io);
                bozo::cancel(std::move(handle), io, 5s, yield[ec]);
            }
        });
        bozo::execute(conn, "SELECT pg_sleep(1000000)"_SQL, yield[ec]);
        EXPECT_EQ(ec, bozo::sqlstate::query_canceled);
    });

    io.run();
}

TEST(cancel, should_stop_cancel_operation_on_zero_timeout) {
    using namespace bozo::literals;
    using namespace std::chrono_literals;
    using namespace hana::literals;

    bozo::io_context io;
    bozo::io_context dummy_io;
    boost::asio::steady_timer timer(io);

    boost::asio::spawn(io, [&io, &timer, &dummy_io](auto yield){
        const bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);
        bozo::error_code ec;
        auto conn = bozo::get_connection(conn_info[io], yield[ec]);
        EXPECT_FALSE(ec);
        boost::asio::spawn(yield, [&io, &timer, handle = get_cancel_handle(conn, dummy_io.get_executor())](auto yield) mutable {
            timer.expires_after(1s);
            bozo::error_code ec;
            timer.async_wait(yield[ec]);
            if (!ec) {
                // Guard is needed since cancel will be served with external
                // system executor, so we need to preserve our io_context from
                // stop until all the operation processed properly
                auto guard = boost::asio::make_work_guard(io);
                bozo::cancel(std::move(handle), io, 0s, yield[ec]);
                EXPECT_EQ(ec, boost::asio::error::timed_out);
            }
        });
        bozo::execute(conn, "SELECT pg_sleep(1000000)"_SQL, 2s, yield[ec]);
        EXPECT_EQ(ec, boost::asio::error::timed_out);
    });

    io.run();
}

} // namespace
