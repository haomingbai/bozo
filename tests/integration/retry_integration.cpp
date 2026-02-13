#include <bozo/connection_info.h>
#include <bozo/query_builder.h>
#include <bozo/request.h>
#include <bozo/shortcuts.h>
#include <bozo/failover/retry.h>

#include <boost/asio/spawn.hpp>
#include <boost/range/adaptor/transformed.hpp>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace {
template <
    typename OidMap = bozo::empty_oid_map,
    typename Statistics = bozo::no_statistics>
struct connection_info_sequence {
    using base_type = bozo::connection_info<OidMap, Statistics>;
    using connection_type = typename base_type::connection_type;

    std::vector<base_type> base_;
    typename std::vector<base_type>::const_iterator i_;

    static std::vector<base_type> make_connection_infos(std::vector<std::string> in) {
        const auto infos = in | boost::adaptors::transformed([](std::string& info){
            return base_type{std::move(info)};
        });
        return {infos.begin(), infos.end()};
    }

    connection_info_sequence(std::vector<std::string> conn_infos)
    : base_(make_connection_infos(std::move(conn_infos))), i_(base_.cbegin()) {}

    connection_info_sequence(const connection_info_sequence&) = delete;
    connection_info_sequence(connection_info_sequence&&) = delete;

    template <typename TimeConstraint, typename Handler>
    void operator ()(bozo::io_context& io, TimeConstraint t, Handler&& handler) {
        if (i_==base_.end()) {
            handler(bozo::error::pq_connection_start_failed, connection_type{});
        } else {
            (*i_++)(io, t, std::forward<Handler>(handler));
        }
    }

    auto operator [](bozo::io_context& io) & {
        return bozo::connection_provider(*this, io);
    }
};

#define ASSERT_REQUEST_OK(ec, conn)\
    ASSERT_FALSE(ec) << ec.message() \
        << "|" << bozo::error_message(conn) \
        << "|" << (!bozo::is_null_recursive(conn) ? bozo::get_error_context(conn) : "") << std::endl


namespace hana = boost::hana;

using namespace testing;
// using namespace bozo::tests;

TEST(request, should_return_success_for_invalid_connection_info_retried_with_valid_connection_info) {
    using namespace bozo::literals;
    using namespace hana::literals;

    bozo::io_context io;
    connection_info_sequence<> conn_info({"invalid connection info", BOZO_PG_TEST_CONNINFO});

    std::vector<int> res;
    bozo::request[bozo::failover::retry(bozo::errc::connection_error)*2](conn_info[io], "SELECT 1"_SQL + " + 1"_SQL, bozo::into(res),
            [&](bozo::error_code ec, auto conn) {
        ASSERT_REQUEST_OK(ec, conn);
        EXPECT_EQ(res.front(), 2);
    });

    io.run();
    EXPECT_EQ(conn_info.i_, conn_info.base_.end());
}

TEST(request, should_return_error_and_bad_connect_for_nonretryable_error) {
    using namespace bozo::literals;
    using namespace hana::literals;

    bozo::io_context io;
    connection_info_sequence<> conn_info({"invalid connection info", BOZO_PG_TEST_CONNINFO});

    std::vector<int> res;
    bozo::request[bozo::failover::retry(bozo::errc::database_readonly)*2](conn_info[io], "SELECT 1"_SQL + " + 1"_SQL, bozo::into(res),
            [&](bozo::error_code ec, [[maybe_unused]] auto conn) {
        EXPECT_TRUE(ec);
    });

    io.run();
    EXPECT_NE(conn_info.i_, conn_info.base_.begin());
    EXPECT_NE(conn_info.i_, conn_info.base_.end());
}

TEST(request, should_return_error_and_bad_connect_for_invalid_connection_info_and_expired_tries) {
    using namespace bozo::literals;
    using namespace hana::literals;

    bozo::io_context io;
    connection_info_sequence<> conn_info({"invalid connection info", "invalid connection info"});

    std::vector<int> res;
    bozo::request[bozo::failover::retry()*2](conn_info[io], "SELECT 1"_SQL + " + 1"_SQL, bozo::into(res),
            [&](bozo::error_code ec, [[maybe_unused]] auto conn) {
        EXPECT_TRUE(ec);
    });

    io.run();
    EXPECT_EQ(conn_info.i_, conn_info.base_.end());
}

} // namespace
