#include <bozo/connection_info.h>
#include <bozo/connection_pool.h>
#include <bozo/query_builder.h>
#include <bozo/request.h>
#include <bozo/execute.h>
#include <bozo/shortcuts.h>
#include <bozo/pg/types/jsonb.h>
#include <bozo/pg/types/ltree.h>

#include <boost/asio/spawn.hpp>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace bozo::pg {

static bool operator ==(const pg::jsonb& lhs, const pg::jsonb& rhs) {
    return lhs.raw_string() == rhs.raw_string();
}

static std::ostream& operator <<(std::ostream& s, const pg::jsonb& v) {
    return s << v.raw_string();
}

static std::ostream& operator <<(std::ostream& s, const pg::ltree& v) {
  return s << v.raw_string();
}

} // namespace bozo::pg

namespace bozo::tests {

struct custom_type {
    std::int16_t number;
    std::string text;
};

static bool operator == (const custom_type& lhs, const custom_type& rhs) {
    return lhs.number == rhs.number && lhs.text == rhs.text;
}

static std::ostream& operator << (std::ostream& s, const custom_type& v) {
    return s << '(' << v.number << ",\"" << v.text <<"\")";
}

struct with_optional {
    BOZO_STD_OPTIONAL<std::int32_t> value;
};

static bool operator ==(const with_optional& lhs, const with_optional& rhs) {
    return lhs.value == rhs.value;
}

static std::ostream& operator <<(std::ostream& s, const with_optional& v) {
    if (v.value) {
        return s << *v.value;
    } else {
        return s << "none";
    }
}

struct with_jsonb {
    bozo::pg::jsonb value;
};

static bool operator ==(const with_jsonb& lhs, const with_jsonb& rhs) {
    return lhs.value == rhs.value;
}

static std::ostream& operator <<(std::ostream& s, const with_jsonb& v) {
    return s << v.value;
}

struct with_ltree {
  bozo::pg::ltree value;
};

static bool operator ==(const with_ltree& lhs, const with_ltree& rhs) {
  return lhs.value == rhs.value;
}

static std::ostream& operator <<(std::ostream& s, const with_ltree& v) {
  return s << v.value;
}

} // namespace bozo::tests

BOOST_HANA_ADAPT_STRUCT(bozo::tests::custom_type, number, text);
BOOST_HANA_ADAPT_STRUCT(bozo::tests::with_optional, value);
BOOST_HANA_ADAPT_STRUCT(bozo::tests::with_jsonb, value);
BOOST_HANA_ADAPT_STRUCT(bozo::tests::with_ltree, value);

BOZO_PG_DEFINE_CUSTOM_TYPE(bozo::tests::custom_type, "custom_type")
BOZO_PG_DEFINE_CUSTOM_TYPE(bozo::tests::with_optional, "with_optional")
BOZO_PG_DEFINE_CUSTOM_TYPE(bozo::tests::with_jsonb, "with_jsonb")
BOZO_PG_DEFINE_CUSTOM_TYPE(bozo::tests::with_ltree, "with_ltree")


#define ASSERT_REQUEST_OK(ec, conn)\
    ASSERT_FALSE(ec) << ec.message() \
        << "|" << bozo::error_message(conn) \
        << "|" << bozo::get_error_context(conn) << std::endl

namespace {

namespace hana = boost::hana;

using namespace testing;
using namespace bozo::tests;

template <typename ...Ts>
using rows_of = std::vector<std::tuple<Ts...>>;

TEST(request, should_return_error_and_bad_connect_for_invalid_connection_info) {
    using namespace bozo::literals;
    using namespace hana::literals;

    bozo::io_context io;
    bozo::connection_info conn_info("invalid connection info");

    bozo::result res;
    bozo::request(conn_info[io], "SELECT 1"_SQL + " + 1"_SQL, std::ref(res),
            [](bozo::error_code ec, auto conn) {
        EXPECT_TRUE(ec);
        EXPECT_TRUE(bozo::connection_bad(conn));
    });

    io.run();
}

TEST(request, should_return_selected_variable) {
    using namespace bozo::literals;
    using namespace hana::literals;

    bozo::io_context io;
    bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);

    bozo::result res;
    const std::string foo = "foo";
    bozo::request(conn_info[io], "SELECT "_SQL + foo, std::ref(res),
            [&](bozo::error_code ec, auto conn) {
        ASSERT_REQUEST_OK(ec, conn);
        ASSERT_EQ(1u, res.size());
        ASSERT_EQ(1u, res[0].size());
        EXPECT_EQ(std::string("foo"), res[0][0].data());
        EXPECT_FALSE(bozo::connection_bad(conn));
    });

    io.run();
}

TEST(request, should_return_selected_string_array) {
    using namespace bozo::literals;
    using namespace hana::literals;

    bozo::io_context io;
    bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);

    const std::vector<std::string> foos = {"foo", "buzz", "bar"};

    rows_of<std::vector<std::string>> res;
    bozo::request(conn_info[io], "SELECT "_SQL + foos, std::back_inserter(res),
            [&](bozo::error_code ec, auto conn) {
        ASSERT_REQUEST_OK(ec, conn);
        ASSERT_EQ(1u, res.size());
        EXPECT_THAT(std::get<0>(res[0]), ElementsAre("foo", "buzz", "bar"));
        EXPECT_FALSE(bozo::connection_bad(conn));
    });

    io.run();
}

TEST(request, should_return_selected_int_array) {
    using namespace bozo::literals;
    using namespace hana::literals;

    bozo::io_context io;
    bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);

    const std::vector<int32_t> foos = {1, 22, 333};

    rows_of<std::vector<int32_t>> res;
    bozo::request(conn_info[io], "SELECT "_SQL + foos, std::back_inserter(res),
            [&](bozo::error_code ec, auto conn) {
        ASSERT_REQUEST_OK(ec, conn);
        ASSERT_EQ(1u, res.size());
        EXPECT_THAT(std::get<0>(res[0]), ElementsAre(1, 22, 333));
        EXPECT_FALSE(bozo::connection_bad(conn));
    });

    io.run();
}

TEST(request, should_fill_oid_map_when_oid_map_is_not_empty) {
    using namespace bozo::literals;
    namespace asio = boost::asio;

    bozo::io_context io;
    constexpr const auto oid_map = bozo::register_types<custom_type>();
    bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);
    bozo::connection_info<std::decay_t<decltype(oid_map)>> conn_info_with_oid_map(BOZO_PG_TEST_CONNINFO);

    asio::spawn(io, [&] (asio::yield_context yield) {
        bozo::result result;
        bozo::error_code ec{};
        auto conn = bozo::request(conn_info[io], "DROP TYPE IF EXISTS custom_type"_SQL, std::ref(result), yield[ec]);
        ASSERT_REQUEST_OK(ec, conn);
        bozo::request(conn, "CREATE TYPE custom_type AS ()"_SQL, std::ref(result), yield[ec]);
        ASSERT_REQUEST_OK(ec, conn);
        auto conn_with_oid_map = bozo::get_connection(conn_info_with_oid_map[io], yield);
        EXPECT_NE(bozo::type_oid<custom_type>(bozo::unwrap_connection(conn_with_oid_map).oid_map()), bozo::null_oid);
    });

    io.run();
}

TEST(request, should_request_with_connection_pool) {
    using namespace bozo::literals;
    namespace asio = boost::asio;

    bozo::io_context io;
    bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);
    const bozo::connection_pool_config config;
    bozo::connection_pool pool(conn_info, config);
    asio::spawn(io, [&] (asio::yield_context yield) {
        bozo::result result;
        bozo::error_code ec{};
        auto conn = bozo::request(pool[io], "SELECT 1"_SQL, std::ref(result), yield[ec]);
        ASSERT_REQUEST_OK(ec, conn);
    });

    io.run();
}

TEST(request, should_call_handler_with_error_for_zero_timeout) {
    using namespace bozo::literals;
    namespace asio = boost::asio;

    bozo::io_context io;
    const bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);
    const bozo::time_traits::duration timeout(0);

    bozo::result res;
    std::atomic_flag called {};
    bozo::request(conn_info[io], "SELECT 1"_SQL, timeout, std::ref(res),
            [&](bozo::error_code ec, auto conn) {
        EXPECT_FALSE(called.test_and_set());
        EXPECT_EQ(ec, boost::asio::error::timed_out);
        EXPECT_TRUE(bozo::connection_bad(conn));
    });

    io.run();
}

TEST(request, should_return_result_for_max_timeout) {
    using namespace bozo::literals;
    namespace asio = boost::asio;

    bozo::io_context io;
    const bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);
    const auto timeout = bozo::time_traits::duration::max();

    rows_of<std::int32_t> res;
    std::atomic_flag called {};
    bozo::request(conn_info[io], "SELECT 1"_SQL, timeout, std::back_inserter(res),
            [&](bozo::error_code ec, auto conn) {
        EXPECT_FALSE(called.test_and_set());
        ASSERT_REQUEST_OK(ec, conn);
        EXPECT_THAT(res, ElementsAre(std::make_tuple(1)));
        EXPECT_FALSE(bozo::connection_bad(conn));
    });

    io.run();
}

TEST(request, should_return_custom_composite) {
    using namespace bozo::literals;
    namespace asio = boost::asio;

    bozo::io_context io;

    asio::spawn(io, [&] (asio::yield_context yield) {
        [&] {
            const bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);
            bozo::error_code ec{};
            auto conn = bozo::execute(conn_info[io],
                "DROP TYPE IF EXISTS custom_type"_SQL, yield[ec]);
            ASSERT_REQUEST_OK(ec, conn);
            bozo::execute(conn, "CREATE TYPE custom_type AS (number int2, text text)"_SQL, yield[ec]);
            ASSERT_REQUEST_OK(ec, conn);
        }();

        const bozo::connection_info conn_info(
            BOZO_PG_TEST_CONNINFO,
            bozo::register_types<custom_type>());

        bozo::rows_of<custom_type> out;
        bozo::error_code ec{};
        auto conn = bozo::request(conn_info[io],
            "SELECT * FROM (VALUES ((1, 'one')::custom_type), ((2, 'two')::custom_type)) AS t (tuple);"_SQL,
            bozo::into(out), yield);

        ASSERT_REQUEST_OK(ec, conn);

        EXPECT_THAT(out, ElementsAre(
            std::make_tuple(custom_type{1, "one"}),
            std::make_tuple(custom_type{2, "two"})
        ));

    });

    io.run();
}

TEST(request, should_send_custom_composite) {
    using namespace bozo::literals;
    namespace asio = boost::asio;

    bozo::io_context io;

    asio::spawn(io, [&] (asio::yield_context yield) {
        [&] {
            const bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);
            bozo::error_code ec{};
            auto conn = bozo::execute(conn_info[io],
                "DROP TYPE IF EXISTS custom_type"_SQL, yield[ec]);
            ASSERT_REQUEST_OK(ec, conn);
            bozo::execute(conn, "CREATE TYPE custom_type AS (number int2, text text)"_SQL, yield[ec]);
            ASSERT_REQUEST_OK(ec, conn);
        }();

        const bozo::connection_info conn_info(
            BOZO_PG_TEST_CONNINFO,
            bozo::register_types<custom_type>());

        bozo::rows_of<custom_type> out;
        bozo::error_code ec{};
        auto conn = bozo::request(conn_info[io],
            "SELECT * FROM (VALUES ("_SQL + custom_type{1, "one"} +
            "), ("_SQL + custom_type{2, "two"} + ")) AS t (tuple);"_SQL,
            bozo::into(out), yield[ec]);

        ASSERT_REQUEST_OK(ec, conn);

        EXPECT_THAT(out, ElementsAre(
            std::make_tuple(custom_type{1, "one"}),
            std::make_tuple(custom_type{2, "two"})
        ));

    });

    io.run();
}

TEST(result, should_send_bytea_properly) {
    using namespace bozo::literals;
    using namespace boost::hana::literals;

    bozo::io_context io;
    bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);

    std::vector<std::tuple<bozo::pg::bytea>> res;
    const std::string foo = "foo";
    auto arr = bozo::pg::bytea({0,1,2,3,4,5,6,7,8,9,0,0});
    EXPECT_EQ(std::size(arr.get())* sizeof(decltype(*arr.get().begin())), bozo::size_of(arr));
    bozo::request(conn_info[io], "SELECT "_SQL + arr, std::back_inserter(res),
            [&](bozo::error_code ec, auto conn) {
        ASSERT_FALSE(ec);
        ASSERT_TRUE(conn);
        ASSERT_EQ(1u, res.size());
        ASSERT_EQ(std::get<0>(res[0]).get(), std::vector<char>({0,1,2,3,4,5,6,7,8,9,0,0}));
    });

    io.run();
}

TEST(request, should_send_empty_optional) {
    using namespace bozo::literals;
    using namespace std::string_literals;
    using namespace std::string_view_literals;
    namespace asio = boost::asio;

    bozo::io_context io;
    const bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);
    const auto timeout = bozo::time_traits::duration::max();
    BOZO_STD_OPTIONAL<std::int32_t> value;

    bozo::rows_of<BOZO_STD_OPTIONAL<std::int32_t>> result;
    std::atomic_flag called {};
    bozo::request(conn_info[io], "SELECT "_SQL + value + "::integer"_SQL, timeout, bozo::into(result),
            [&](bozo::error_code ec, auto conn) {
        EXPECT_FALSE(called.test_and_set());
        ASSERT_REQUEST_OK(ec, conn);
        EXPECT_THAT(result, ElementsAre(value));
        EXPECT_FALSE(bozo::connection_bad(conn));
    });

    io.run();
}

TEST(request, should_send_and_receive_empty_optional) {
    using namespace bozo::literals;
    using namespace std::string_literals;
    using namespace std::string_view_literals;
    namespace asio = boost::asio;

    bozo::io_context io;
    const bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);
    const auto timeout = bozo::time_traits::duration::max();
    BOZO_STD_OPTIONAL<std::int32_t> value;

    bozo::rows_of<BOZO_STD_OPTIONAL<std::int32_t>> result;
    std::atomic_flag called {};
    bozo::request(conn_info[io], "SELECT "_SQL + value + "::integer"_SQL, timeout, bozo::into(result),
            [&](bozo::error_code ec, auto conn) {
        EXPECT_FALSE(called.test_and_set());
        ASSERT_REQUEST_OK(ec, conn);
        EXPECT_THAT(result, ElementsAre(value));
        EXPECT_FALSE(bozo::connection_bad(conn));
    });

    io.run();
}

TEST(request, should_send_and_receive_interval) {
    using namespace bozo::literals;

    const auto interval = bozo::pg::interval(239278272013014LL);

    bozo::io_context io;
    const bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);

    bozo::rows_of<bozo::pg::interval> result;
    auto query = "SELECT '2769 days 10 hours 11 minutes 12 seconds 13014 microseconds'::interval + "_SQL + interval;
    bozo::request(conn_info[io], query, bozo::into(result), [&](bozo::error_code ec, auto conn) {
        ASSERT_REQUEST_OK(ec, conn);
        ASSERT_EQ(result.size(), 1u);
        EXPECT_EQ(std::get<0>(result[0]), bozo::pg::interval(478556544026028LL));
    });

    io.run();
}

TEST(request, should_send_and_receive_composite_with_empty_optional) {
    using namespace bozo::literals;
    namespace asio = boost::asio;

    bozo::io_context io;

    asio::spawn(io, [&] (asio::yield_context yield) {
        [&] {
            const bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);
            bozo::error_code ec;
            auto conn = bozo::execute(conn_info[io],
                "DROP TYPE IF EXISTS with_optional"_SQL, yield[ec]);
            ASSERT_REQUEST_OK(ec, conn);
            bozo::execute(conn, "CREATE TYPE with_optional AS (value integer)"_SQL, yield[ec]);
            ASSERT_REQUEST_OK(ec, conn);
        } ();

        const bozo::connection_info conn_info(
            BOZO_PG_TEST_CONNINFO,
            bozo::register_types<with_optional>()
        );

        const with_optional value;
        bozo::rows_of<with_optional> result;
        bozo::error_code ec;
        auto conn = bozo::request(conn_info[io], "SELECT "_SQL + value + "::with_optional"_SQL,
                                 bozo::into(result), yield[ec]);

        ASSERT_REQUEST_OK(ec, conn);

        EXPECT_THAT(result, ElementsAre(value));
    });

    io.run();
}

TEST(request, should_send_and_receive_jsonb) {
    using namespace bozo::literals;
    using namespace std::string_literals;
    using namespace std::string_view_literals;
    namespace asio = boost::asio;

    bozo::io_context io;
    const bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);
    const auto timeout = bozo::time_traits::duration::max();
    std::string value = R"({"foo": "bar"})";

    bozo::rows_of<bozo::pg::jsonb> result;
    std::atomic_flag called {};
    bozo::request(conn_info[io], "SELECT "_SQL + value + "::jsonb"_SQL, timeout, bozo::into(result),
            [&](bozo::error_code ec, auto conn) {
        EXPECT_FALSE(called.test_and_set());
        ASSERT_REQUEST_OK(ec, conn);
        EXPECT_THAT(result, ElementsAre(bozo::pg::jsonb(value)));
        EXPECT_FALSE(bozo::connection_bad(conn));
    });

    io.run();
}

TEST(request, should_send_and_receive_composite_with_jsonb_field) {
    using namespace bozo::literals;
    using namespace std::string_literals;
    using namespace std::string_view_literals;
    namespace asio = boost::asio;

    bozo::io_context io;

    asio::spawn(io, [&] (asio::yield_context yield) {
        [&] {
            const bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);
            bozo::error_code ec;
            auto conn = bozo::execute(conn_info[io],
                "DROP TYPE IF EXISTS with_jsonb"_SQL, yield[ec]);
            ASSERT_REQUEST_OK(ec, conn);
            bozo::execute(conn, "CREATE TYPE with_jsonb AS (value jsonb)"_SQL, yield[ec]);
            ASSERT_REQUEST_OK(ec, conn);
        } ();

        const bozo::connection_info conn_info(
            BOZO_PG_TEST_CONNINFO,
            bozo::register_types<with_jsonb>()
        );

        const with_jsonb value {{R"({"foo": "bar"})"}};
        bozo::rows_of<with_jsonb> result;
        bozo::error_code ec;
        auto conn = bozo::request(conn_info[io], "SELECT "_SQL + value + "::with_jsonb"_SQL,
                                 bozo::into(result), yield[ec]);

        ASSERT_REQUEST_OK(ec, conn);

        EXPECT_THAT(result, ElementsAre(with_jsonb {{R"({"foo": "bar"})"}}));
    });

    io.run();
}

TEST(request, should_send_and_receive_ltree) {
    using namespace bozo::literals;
    using namespace std::string_literals;
    using namespace std::string_view_literals;
    namespace asio = boost::asio;

    bozo::io_context io;

    asio::spawn(io, [&] (asio::yield_context yield) {
        [&] {
            const bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);
            bozo::error_code ec;
            auto conn = bozo::execute(conn_info[io],
                                     "DROP TYPE IF EXISTS with_ltree"_SQL, yield[ec]);
            ASSERT_REQUEST_OK(ec, conn);
            bozo::execute(conn, "DROP EXTENSION IF EXISTS ltree"_SQL, yield[ec]);
            ASSERT_REQUEST_OK(ec, conn);
            bozo::execute(conn, "CREATE EXTENSION ltree"_SQL, yield[ec]);
            ASSERT_REQUEST_OK(ec, conn);
        } ();

        const bozo::connection_info conn_info(
            BOZO_PG_TEST_CONNINFO,
            bozo::register_types<bozo::pg::ltree>()
        );

        const auto timeout = bozo::time_traits::duration::max();
        std::string value = "1.2.3.4";

        bozo::error_code ec;
        bozo::rows_of<bozo::pg::ltree> result;
        auto conn = bozo::request(conn_info[io], "SELECT "_SQL + value + "::ltree"_SQL,
                                 timeout, bozo::into(result), yield[ec]);

        ASSERT_REQUEST_OK(ec, conn);
        EXPECT_THAT(result, ElementsAre(bozo::pg::ltree(value)));
        EXPECT_FALSE(bozo::connection_bad(conn));
    });

    io.run();
}

TEST(request, should_send_and_receive_composite_with_ltree_field) {
    using namespace bozo::literals;
    using namespace std::string_literals;
    using namespace std::string_view_literals;
    namespace asio = boost::asio;

    bozo::io_context io;

    asio::spawn(io, [&] (asio::yield_context yield) {
        [&] {
            const bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);
            bozo::error_code ec;
            auto conn = bozo::execute(conn_info[io],
                                     "DROP TYPE IF EXISTS with_ltree"_SQL, yield[ec]);
            ASSERT_REQUEST_OK(ec, conn);
            bozo::execute(conn, "DROP EXTENSION IF EXISTS ltree"_SQL, yield[ec]);
            ASSERT_REQUEST_OK(ec, conn);
            bozo::execute(conn, "CREATE EXTENSION ltree"_SQL, yield[ec]);
            ASSERT_REQUEST_OK(ec, conn);
        } ();

        [&] {
            const bozo::connection_info conn_info(
                BOZO_PG_TEST_CONNINFO,
                bozo::register_types<bozo::pg::ltree>()
            );

            bozo::error_code ec;
            auto conn = bozo::execute(conn_info[io],
                                     "DROP TYPE IF EXISTS with_ltree"_SQL, yield[ec]);
            ASSERT_REQUEST_OK(ec, conn);
            bozo::execute(conn, "CREATE TYPE with_ltree AS (value ltree)"_SQL, yield[ec]);
            ASSERT_REQUEST_OK(ec, conn);
        } ();

        const bozo::connection_info conn_info(
            BOZO_PG_TEST_CONNINFO,
            bozo::register_types<bozo::pg::ltree, with_ltree>()
        );

        const with_ltree value {{"1.2.3.4"}};
        bozo::rows_of<with_ltree> result;
        bozo::error_code ec;
        auto conn = bozo::request(conn_info[io], "SELECT "_SQL + value + "::with_ltree"_SQL,
                                 bozo::into(result), yield[ec]);

        ASSERT_REQUEST_OK(ec, conn);

        EXPECT_THAT(result, ElementsAre(with_ltree {{"1.2.3.4"}}));
    });

    io.run();
}

} // namespace
