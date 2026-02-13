#include <bozo/connection_info.h>
#include <bozo/query_builder.h>
#include <bozo/result.h>
#include <bozo/request.h>
#include <bozo/transaction.h>

#include <boost/asio/spawn.hpp>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace {

namespace asio = boost::asio;

using namespace testing;

TEST(transaction_integration, create_schema_in_transaction_and_commit_then_table_should_exist) {
    using namespace bozo::literals;

    bozo::io_context io;
    bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);

    asio::spawn(io, [&] (asio::yield_context yield) {
        auto transaction = bozo::begin(conn_info[io], yield);
        ASSERT_TRUE(transaction);
        bozo::result result;
        bozo::request(transaction, "DROP SCHEMA IF EXISTS bozo_test CASCADE;"_SQL, std::ref(result), yield);
        bozo::request(transaction, "CREATE SCHEMA bozo_test;"_SQL, std::ref(result), yield);
        auto connection = bozo::commit(std::move(transaction), yield);
        bozo::request(connection, "DROP SCHEMA bozo_test;"_SQL, std::ref(result), yield);
    });

    io.run();
}

TEST(transaction_integration, create_schema_in_transaction_and_rollback_then_table_should_not_exist) {
    using namespace bozo::literals;

    bozo::io_context io;
    bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);

    asio::spawn(io, [&] (asio::yield_context yield) {
        auto transaction = bozo::begin(conn_info[io], yield);
        bozo::result result;
        bozo::request(transaction, "DROP SCHEMA IF EXISTS bozo_test CASCADE;"_SQL, std::ref(result), yield);
        bozo::request(transaction, "CREATE SCHEMA bozo_test;"_SQL, std::ref(result), yield);
        auto connection = bozo::rollback(std::move(transaction), yield);
        bozo::error_code ec;
        bozo::request(connection, "DROP SCHEMA bozo_test;"_SQL, std::ref(result), yield[ec]);
        EXPECT_EQ(ec, bozo::error_condition(bozo::sqlstate::invalid_schema_name));
    });

    io.run();
}

TEST(transaction_integration, transaction_level_options_should_not_cause_sql_syntax_errors) {
    bozo::io_context io;
    bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);

    asio::spawn(io, [&] (asio::yield_context yield) {
        auto level = bozo::isolation_level::serializable;
        auto const options = bozo::make_options(bozo::transaction_options::isolation_level = level);
        bozo::error_code ec;
        auto transaction = bozo::begin.with_transaction_options(options)(conn_info[io], bozo::none, yield[ec]);

        static_assert(std::is_same_v<decltype(bozo::get_transaction_isolation_level(transaction)), decltype(level)>);
        static_assert(bozo::is_none<decltype(bozo::get_transaction_mode(transaction))>::value);
        static_assert(bozo::is_none<decltype(bozo::get_transaction_deferrability(transaction))>::value);

        EXPECT_FALSE(ec);
        bozo::rollback(std::move(transaction), yield[ec]);
        EXPECT_FALSE(ec);
    });

    asio::spawn(io, [&] (asio::yield_context yield) {
        auto level = bozo::isolation_level::repeatable_read;
        auto const options = bozo::make_options(bozo::transaction_options::isolation_level = level);
        bozo::error_code ec;
        auto transaction = bozo::begin.with_transaction_options(options)(conn_info[io], bozo::none, yield[ec]);

        static_assert(std::is_same_v<decltype(bozo::get_transaction_isolation_level(transaction)), decltype(level)>);
        static_assert(bozo::is_none<decltype(bozo::get_transaction_mode(transaction))>::value);
        static_assert(bozo::is_none<decltype(bozo::get_transaction_deferrability(transaction))>::value);

        EXPECT_FALSE(ec);
        bozo::rollback(std::move(transaction), yield[ec]);
        EXPECT_FALSE(ec);
    });

    asio::spawn(io, [&] (asio::yield_context yield) {
        auto level = bozo::isolation_level::read_committed;
        auto const options = bozo::make_options(bozo::transaction_options::isolation_level = level);
        bozo::error_code ec;
        auto transaction = bozo::begin.with_transaction_options(options)(conn_info[io], bozo::none, yield[ec]);

        static_assert(std::is_same_v<decltype(bozo::get_transaction_isolation_level(transaction)), decltype(level)>);
        static_assert(bozo::is_none<decltype(bozo::get_transaction_mode(transaction))>::value);
        static_assert(bozo::is_none<decltype(bozo::get_transaction_deferrability(transaction))>::value);

        EXPECT_FALSE(ec);
        bozo::rollback(std::move(transaction), yield[ec]);
        EXPECT_FALSE(ec);
    });

    asio::spawn(io, [&] (asio::yield_context yield) {
        auto level = bozo::isolation_level::read_uncommitted;
        auto const options = bozo::make_options(bozo::transaction_options::isolation_level = level);
        bozo::error_code ec;
        auto transaction = bozo::begin.with_transaction_options(options)(conn_info[io], bozo::none, yield[ec]);

        static_assert(std::is_same_v<decltype(bozo::get_transaction_isolation_level(transaction)), decltype(level)>);
        static_assert(bozo::is_none<decltype(bozo::get_transaction_mode(transaction))>::value);
        static_assert(bozo::is_none<decltype(bozo::get_transaction_deferrability(transaction))>::value);

        EXPECT_FALSE(ec);
        bozo::rollback(std::move(transaction), yield[ec]);
        EXPECT_FALSE(ec);
    });

    io.run();
}

TEST(transaction_integration, transaction_mode_options_should_not_cause_sql_syntax_errors) {
    bozo::io_context io;
    bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);

    asio::spawn(io, [&] (asio::yield_context yield) {
        auto mode = bozo::transaction_mode::read_write;
        auto const options = bozo::make_options(bozo::transaction_options::mode = mode);
        bozo::error_code ec;
        auto transaction = bozo::begin.with_transaction_options(options)(conn_info[io], bozo::none, yield[ec]);

        static_assert(bozo::is_none<decltype(bozo::get_transaction_isolation_level(transaction))>::value);
        static_assert(std::is_same_v<decltype(bozo::get_transaction_mode(transaction)), decltype(mode)>);
        static_assert(bozo::is_none<decltype(bozo::get_transaction_deferrability(transaction))>::value);

        EXPECT_FALSE(ec);
        bozo::rollback(std::move(transaction), yield[ec]);
        EXPECT_FALSE(ec);
    });

    asio::spawn(io, [&] (asio::yield_context yield) {
        auto mode = bozo::transaction_mode::read_only;
        auto const options = bozo::make_options(bozo::transaction_options::mode = mode);
        bozo::error_code ec;
        auto transaction = bozo::begin.with_transaction_options(options)(conn_info[io], bozo::none, yield[ec]);

        static_assert(bozo::is_none<decltype(bozo::get_transaction_isolation_level(transaction))>::value);
        static_assert(std::is_same_v<decltype(bozo::get_transaction_mode(transaction)), decltype(mode)>);
        static_assert(bozo::is_none<decltype(bozo::get_transaction_deferrability(transaction))>::value);

        EXPECT_FALSE(ec);
        bozo::rollback(std::move(transaction), yield[ec]);
        EXPECT_FALSE(ec);
    });

    io.run();
}

TEST(transaction_integration, transaction_deferrability_options_should_not_generate_syntax_errors) {
    using bozo::transaction_options;
    bozo::io_context io;
    bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);

    asio::spawn(io, [&] (asio::yield_context yield) {
        auto defer = bozo::deferrable;
        auto const options = bozo::make_options(bozo::transaction_options::deferrability = defer);
        bozo::error_code ec;
        auto transaction = bozo::begin.with_transaction_options(options)(conn_info[io], yield[ec]);

        static_assert(bozo::is_none<decltype(bozo::get_transaction_isolation_level(transaction))>::value);
        static_assert(bozo::is_none<decltype(bozo::get_transaction_mode(transaction))>::value);
        static_assert(std::is_same_v<decltype(bozo::get_transaction_deferrability(transaction)), decltype(defer)>);

        EXPECT_FALSE(ec);
        bozo::rollback(std::move(transaction), yield[ec]);
        EXPECT_FALSE(ec);
    });

    asio::spawn(io, [&] (asio::yield_context yield) {
        auto defer = !bozo::deferrable;
        auto const options = bozo::make_options(bozo::transaction_options::deferrability = defer);
        bozo::error_code ec;
        auto transaction = bozo::begin.with_transaction_options(options)(conn_info[io], yield[ec]);

        static_assert(bozo::is_none<decltype(bozo::get_transaction_isolation_level(transaction))>::value);
        static_assert(bozo::is_none<decltype(bozo::get_transaction_mode(transaction))>::value);
        static_assert(std::is_same_v<decltype(bozo::get_transaction_deferrability(transaction)), decltype(defer)>);

        EXPECT_FALSE(ec);
        bozo::rollback(std::move(transaction), yield[ec]);
        EXPECT_FALSE(ec);
    });

    io.run();
}

} // namespace
