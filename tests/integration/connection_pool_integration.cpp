#include <bozo/connection_info.h>
#include <bozo/connection_pool.h>
#include <bozo/query_builder.h>
#include <bozo/request.h>
#include <bozo/shortcuts.h>

#include <boost/asio/spawn.hpp>

#include <algorithm>
#include <future>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace {

namespace asio = boost::asio;

using namespace testing;

TEST(connection_pool_integration, get_connection_twice_should_get_the_same) {
    using namespace bozo::literals;
    using namespace std::chrono_literals;

    bozo::io_context io;
    bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);
    bozo::connection_pool_config config;
    config.capacity = 1;
    config.queue_capacity = 0;
    bozo::connection_pool pool(conn_info, config, !bozo::thread_safe);

    asio::spawn(io, [&] (asio::yield_context yield) {
        const auto get_pg_backend_pid = [&] (int& pg_backend_pid) {
            bozo::rows_of<int> result;
            bozo::error_code ec;
            const auto conn = bozo::request(
                pool[io],
                "SELECT pg_backend_pid()"_SQL,
                bozo::deadline(1s),
                bozo::into(result),
                yield[ec]
            );

            ASSERT_FALSE(ec) << ec.message();
            ASSERT_FALSE(bozo::is_null_recursive(conn));
            ASSERT_EQ(1u, result.size());

            pg_backend_pid = std::get<0>(result[0]);
        };

        int pg_backend_pid1 = 0;
        get_pg_backend_pid(pg_backend_pid1);
        int pg_backend_pid2 = 0;
        get_pg_backend_pid(pg_backend_pid2);

        ASSERT_NE(pg_backend_pid1, 0);
        EXPECT_EQ(pg_backend_pid1, pg_backend_pid2);
    });

    io.run();
}

TEST(connection_pool_integration, request_should_wait_until_connection_is_available) {
    using namespace bozo::literals;
    using namespace std::chrono_literals;

    bozo::io_context io;
    bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);
    bozo::connection_pool_config config;
    config.capacity = 1;
    config.queue_capacity = 1;
    bozo::connection_pool pool(conn_info, config, !bozo::thread_safe);
    std::array<int, 2> pg_backend_pids {{0, 0}};

    for (auto& pg_backend_pid : pg_backend_pids) {
        asio::spawn(io, [&, pg_backend_pid = &pg_backend_pid] (asio::yield_context yield) {
            bozo::rows_of<int> result;
            bozo::error_code ec;
            const auto conn = bozo::request(
                pool[io],
                "SELECT pg_backend_pid()"_SQL,
                bozo::deadline(1s),
                bozo::into(result),
                yield[ec]
            );

            ASSERT_FALSE(ec) << ec.message();
            ASSERT_FALSE(bozo::is_null_recursive(conn));
            ASSERT_EQ(1u, result.size());

            *pg_backend_pid = std::get<0>(result[0]);
        });
    }

    io.run();

    ASSERT_NE(pg_backend_pids[0], 0);
    EXPECT_EQ(pg_backend_pids[0], pg_backend_pids[1]);
}

TEST(connection_pool_integration, should_serve_concurrent_requests) {
    using namespace bozo::literals;
    using namespace std::chrono_literals;

    std::vector<bozo::io_context> ios(3);
    bozo::connection_info conn_info(BOZO_PG_TEST_CONNINFO);
    bozo::connection_pool_config config;
    config.capacity = 1;
    config.queue_capacity = 2;
    bozo::connection_pool pool(conn_info, config);
    std::vector<std::future<int>> futures;

    for (auto& io : ios) {
        futures.emplace_back(std::async(std::launch::async, [&, io_ptr = &io] {
            auto& io = *io_ptr;
            auto guard = boost::asio::make_work_guard(io);
            int pg_backend_pid = 0;

            asio::spawn(io, [&] (asio::yield_context yield) {
                bozo::rows_of<int> result;
                bozo::error_code ec;
                const auto conn = bozo::request(
                    pool[io],
                    "SELECT pg_backend_pid()"_SQL,
                    bozo::deadline(1s),
                    bozo::into(result),
                    yield[ec]
                );

                ASSERT_FALSE(ec) << ec.message();
                ASSERT_FALSE(bozo::is_null_recursive(conn));
                ASSERT_EQ(1u, result.size());

                pg_backend_pid = std::get<0>(result[0]);
                guard.reset();
            });

            io.run();

            return pg_backend_pid;
        }));
    }

    std::vector<int> results;
    std::transform(futures.begin(), futures.end(), std::back_inserter(results), [] (auto& v) { return v.get(); });
    ASSERT_NE(results.front(), 0);
    EXPECT_THAT(results, Each(results.front()));
}

} // namespace
