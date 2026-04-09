#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>

#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>

#include "ozo/ext/std/optional.h"
#include "ozo/query_builder.h"
#include "ozo/result.h"
#include "postgresql_test_support.h"

namespace {

using bozo::postgresql::PostgreSqlTaskFactory;
using bozo::postgresql::PostgreSqlTaskOptions;
using bozo::postgresql::PostgreSqlTaskPhase;
using bozo::postgresql::PostgreSqlTaskPoolFactory;
using bozo::postgresql::PostgreSqlTaskPoolOptions;
using ozo::literals::operator""_SQL;
using testing::ElementsAre;

PostgreSqlTaskFactory MakeDirectFactory(boost::asio::io_context &io,
                                        const std::string &conninfo) {
  PostgreSqlTaskOptions options;
  options.operation_timeout = std::chrono::seconds(5);
  return PostgreSqlTaskFactory(io.get_executor(), conninfo, options);
}

PostgreSqlTaskPoolFactory MakePoolFactory(boost::asio::io_context &io,
                                          const std::string &conninfo,
                                          std::size_t capacity) {
  PostgreSqlTaskPoolOptions options;
  options.operation_timeout = std::chrono::seconds(5);
  options.pool_config.capacity = capacity;
  options.pool_config.queue_capacity = capacity;
  return PostgreSqlTaskPoolFactory(io.get_executor(), conninfo, options);
}

TEST(PostgreSqlTaskIntegrationTest,
     RequestSupportsMultiRowMultiColumnOptionalAndRawResult) {
  const auto conninfo = bozo::test::RequireTestConninfo();
  if (conninfo.empty()) {
    GTEST_SKIP() << "BOZO_PG_TEST_CONNINFO is not set";
  }
  boost::asio::io_context io;
  auto task = MakeDirectFactory(io, conninfo).Create();

  ozo::rows_of<int, std::string, std::optional<int>> rows;
  const auto request_result = bozo::test::RunTaskOperation(io, [&](auto cb) {
    return task->Request(
        R"(SELECT * FROM (
                 VALUES
                   (1::integer, E'alpha\nfirst line', NULL::integer),
                   (2::integer, E'beta\nsecond line\nthird line', 7::integer)
               ) AS t(id, note, score)
               ORDER BY id)"_SQL,
        ozo::into(rows), std::move(cb));
  });

  ASSERT_TRUE(request_result.Ok());
  EXPECT_EQ(request_result.GetState().GetPhase(),
            PostgreSqlTaskPhase::kConnection);
  EXPECT_THAT(
      rows,
      ElementsAre(std::make_tuple(1, "alpha\nfirst line", std::nullopt),
                  std::make_tuple(2, "beta\nsecond line\nthird line", 7)));

  ozo::result raw_result_rows;
  const auto raw_result = bozo::test::RunTaskOperation(io, [&](auto cb) {
    return task->RequestRaw("SELECT current_database(), current_schema()"_SQL,
                            raw_result_rows, std::move(cb));
  });

  ASSERT_TRUE(raw_result.Ok());
  ASSERT_EQ(raw_result_rows.size(), 1U);
  ASSERT_EQ(raw_result_rows[0].size(), 2U);
  EXPECT_FALSE(std::string(raw_result_rows[0][0].data()).empty());
  EXPECT_EQ(std::string(raw_result_rows[0][1].data()), "public");

  const auto close_result = bozo::test::RunTaskOperation(
      io, [&](auto cb) { return task->Close(std::move(cb)); });
  EXPECT_TRUE(close_result.GetState().IsClosed());
}

TEST(PostgreSqlTaskIntegrationTest,
     RequestValueAndRequestRawValueDeliverDecodedOutputToCallback) {
  const auto conninfo = bozo::test::RequireTestConninfo();
  if (conninfo.empty()) {
    GTEST_SKIP() << "BOZO_PG_TEST_CONNINFO is not set";
  }
  boost::asio::io_context io;
  auto task = MakeDirectFactory(io, conninfo).Create();

  using TypedRows = ozo::rows_of<int, std::string, std::optional<int>>;
  struct TypedObservation {
    bozo::postgresql::PostgreSqlTaskResult result;
    TypedRows rows;
  };
  std::promise<TypedObservation> typed_promise;
  auto typed_future = typed_promise.get_future();

  const auto typed_ec = task->RequestValue<TypedRows>(
      R"(SELECT * FROM (
               VALUES
                 (1::integer, E'callback\nowned', NULL::integer),
                 (2::integer, E'callback\nowned\nagain', 9::integer)
             ) AS t(id, note, score)
             ORDER BY id)"_SQL,
      [&typed_promise](const bozo::postgresql::PostgreSqlTaskResult &result,
                       const TypedRows &rows) mutable {
        typed_promise.set_value(TypedObservation{result, rows});
      });
  ASSERT_FALSE(typed_ec);

  io.run();
  io.restart();

  const auto typed_observation = typed_future.get();
  ASSERT_TRUE(typed_observation.result.Ok());
  EXPECT_EQ(typed_observation.result.GetState().GetPhase(),
            PostgreSqlTaskPhase::kConnection);
  EXPECT_THAT(
      typed_observation.rows,
      ElementsAre(std::make_tuple(1, "callback\nowned", std::nullopt),
                  std::make_tuple(2, "callback\nowned\nagain", 9)));

  struct RawObservation {
    bozo::postgresql::PostgreSqlTaskResult result;
    std::size_t column_count{0};
    std::string schema;
  };
  std::promise<RawObservation> raw_promise;
  auto raw_future = raw_promise.get_future();

  const auto raw_ec = task->RequestRawValue(
      "SELECT current_database(), current_schema()"_SQL,
      [&raw_promise](const bozo::postgresql::PostgreSqlTaskResult &result,
                     const ozo::result &raw) mutable {
        raw_promise.set_value(RawObservation{
            result, raw.empty() ? 0U : raw[0].size(),
            raw.empty() ? std::string{} : std::string(raw[0][1].data())});
      });
  ASSERT_FALSE(raw_ec);

  io.run();
  io.restart();

  const auto raw_observation = raw_future.get();
  ASSERT_TRUE(raw_observation.result.Ok());
  EXPECT_EQ(raw_observation.column_count, 2U);
  EXPECT_EQ(raw_observation.schema, "public");

  const auto close_result = bozo::test::RunTaskOperation(
      io, [&](auto cb) { return task->Close(std::move(cb)); });
  EXPECT_TRUE(close_result.GetState().IsClosed());
}

TEST(PostgreSqlTaskIntegrationTest,
     TransactionCommitAndRollbackAffectVisibility) {
  const auto conninfo = bozo::test::RequireTestConninfo();
  if (conninfo.empty()) {
    GTEST_SKIP() << "BOZO_PG_TEST_CONNINFO is not set";
  }
  boost::asio::io_context io;
  auto task = MakeDirectFactory(io, conninfo).Create();
  auto verifier = MakeDirectFactory(io, conninfo).Create();

  ASSERT_TRUE(bozo::test::RunTaskOperation(io, [&](auto cb) {
                return task->Execute(
                    "DROP TABLE IF EXISTS bozo_it_visibility;"_SQL,
                    std::move(cb));
              }).Ok());
  ASSERT_TRUE(
      bozo::test::RunTaskOperation(io, [&](auto cb) {
        return task->Execute(
            "CREATE TABLE bozo_it_visibility (id integer PRIMARY KEY, note text);"_SQL,
            std::move(cb));
      }).Ok());

  const auto start_transaction = bozo::test::RunTaskOperation(
      io, [&](auto cb) { return task->StartTransaction(std::move(cb)); });
  ASSERT_TRUE(start_transaction.Ok());
  EXPECT_TRUE(start_transaction.GetState().InTransaction());

  ASSERT_TRUE(bozo::test::RunTaskOperation(io, [&](auto cb) {
                return task->Execute(
                    R"(INSERT INTO bozo_it_visibility (id, note)
                           VALUES (1, E'committed\nvalue'))"_SQL,
                    std::move(cb));
              }).Ok());

  const auto commit_result = bozo::test::RunTaskOperation(
      io, [&](auto cb) { return task->CommitTransaction(std::move(cb)); });
  ASSERT_TRUE(commit_result.Ok());
  EXPECT_EQ(commit_result.GetState().GetPhase(),
            PostgreSqlTaskPhase::kConnection);

  ozo::rows_of<std::int64_t> count_rows;
  const auto verify_after_commit =
      bozo::test::RunTaskOperation(io, [&](auto cb) {
        return verifier->Request("SELECT COUNT(*) FROM bozo_it_visibility;"_SQL,
                                 ozo::into(count_rows), std::move(cb));
      });
  ASSERT_TRUE(verify_after_commit.Ok())
      << verify_after_commit.GetTaskError().message() << " | "
      << verify_after_commit.GetOzoError().message() << " | "
      << verify_after_commit.GetState().GetLastErrorMessage() << " | "
      << verify_after_commit.GetState().GetLastErrorContext();
  ASSERT_EQ(count_rows.size(), 1U);
  EXPECT_EQ(std::get<0>(count_rows.front()), 1);

  ASSERT_TRUE(bozo::test::RunTaskOperation(io, [&](auto cb) {
                return task->StartTransaction(std::move(cb));
              }).Ok());
  ASSERT_TRUE(bozo::test::RunTaskOperation(io, [&](auto cb) {
                return task->Execute(
                    R"(INSERT INTO bozo_it_visibility (id, note)
                           VALUES (2, E'rolled\nback'))"_SQL,
                    std::move(cb));
              }).Ok());
  ASSERT_TRUE(bozo::test::RunTaskOperation(io, [&](auto cb) {
                return task->RollbackTransaction(std::move(cb));
              }).Ok());

  count_rows.clear();
  const auto verify_after_rollback =
      bozo::test::RunTaskOperation(io, [&](auto cb) {
        return verifier->Request("SELECT COUNT(*) FROM bozo_it_visibility;"_SQL,
                                 ozo::into(count_rows), std::move(cb));
      });
  ASSERT_TRUE(verify_after_rollback.Ok())
      << verify_after_rollback.GetTaskError().message() << " | "
      << verify_after_rollback.GetOzoError().message() << " | "
      << verify_after_rollback.GetState().GetLastErrorMessage() << " | "
      << verify_after_rollback.GetState().GetLastErrorContext();
  ASSERT_EQ(count_rows.size(), 1U);
  EXPECT_EQ(std::get<0>(count_rows.front()), 1);

  ASSERT_TRUE(bozo::test::RunTaskOperation(io, [&](auto cb) {
                return task->Execute(
                    "DROP TABLE IF EXISTS bozo_it_visibility;"_SQL,
                    std::move(cb));
              }).Ok());
  EXPECT_TRUE(bozo::test::RunTaskOperation(
                  io, [&](auto cb) { return task->Close(std::move(cb)); })
                  .GetState()
                  .IsClosed());
  EXPECT_TRUE(bozo::test::RunTaskOperation(
                  io, [&](auto cb) { return verifier->Close(std::move(cb)); })
                  .GetState()
                  .IsClosed());
}

TEST(PostgreSqlTaskIntegrationTest,
     PoolTasksHoldDedicatedConnectionsUntilClose) {
  const auto conninfo = bozo::test::RequireTestConninfo();
  if (conninfo.empty()) {
    GTEST_SKIP() << "BOZO_PG_TEST_CONNINFO is not set";
  }
  boost::asio::io_context io;
  auto factory = MakePoolFactory(io, conninfo, 2);
  auto task_one = factory.Create();
  auto task_two = factory.Create();

  auto run_backend_pid =
      [&](const std::shared_ptr<bozo::postgresql::PostgreSqlTask> &task) {
        ozo::rows_of<int> rows;
        const auto result = bozo::test::RunTaskOperation(io, [&](auto cb) {
          return task->Request("SELECT pg_backend_pid()"_SQL, ozo::into(rows),
                               std::move(cb));
        });
        EXPECT_TRUE(result.Ok());
        EXPECT_EQ(result.GetState().GetPhase(),
                  PostgreSqlTaskPhase::kConnection);
        EXPECT_EQ(rows.size(), 1U);
        return std::get<0>(rows.front());
      };

  const int task_one_pid_first = run_backend_pid(task_one);
  const int task_two_pid_first = run_backend_pid(task_two);
  const int task_one_pid_second = run_backend_pid(task_one);
  const int task_two_pid_second = run_backend_pid(task_two);

  EXPECT_NE(task_one_pid_first, 0);
  EXPECT_NE(task_two_pid_first, 0);
  EXPECT_EQ(task_one_pid_first, task_one_pid_second);
  EXPECT_EQ(task_two_pid_first, task_two_pid_second);
  EXPECT_NE(task_one_pid_first, task_two_pid_first);

  EXPECT_TRUE(bozo::test::RunTaskOperation(
                  io, [&](auto cb) { return task_one->Close(std::move(cb)); })
                  .GetState()
                  .IsClosed());
  EXPECT_TRUE(bozo::test::RunTaskOperation(
                  io, [&](auto cb) { return task_two->Close(std::move(cb)); })
                  .GetState()
                  .IsClosed());
}

TEST(PostgreSqlTaskIntegrationTest,
     SyntaxErrorOutsideTransactionKeepsConnectionUsableForLaterQueries) {
  const auto conninfo = bozo::test::RequireTestConninfo();
  if (conninfo.empty()) {
    GTEST_SKIP() << "BOZO_PG_TEST_CONNINFO is not set";
  }

  boost::asio::io_context io;
  auto task = MakeDirectFactory(io, conninfo).Create();

  const auto invalid_result = bozo::test::RunTaskOperation(io, [&](auto cb) {
    return task->Execute("SELECT FROM broken_sql_statement;"_SQL,
                         std::move(cb));
  });

  ASSERT_FALSE(invalid_result.Ok());
  EXPECT_FALSE(invalid_result.GetTaskError());
  EXPECT_TRUE(invalid_result.GetOzoError());
  EXPECT_EQ(invalid_result.GetState().GetPhase(),
            PostgreSqlTaskPhase::kConnection);
  EXPECT_TRUE(invalid_result.GetState().HasConnection());
  EXPECT_FALSE(invalid_result.GetState().InTransaction());
  EXPECT_FALSE(invalid_result.GetState().GetLastErrorMessage().empty());

  using Rows = ozo::rows_of<int, std::string, std::optional<int>>;
  std::promise<std::pair<bozo::postgresql::PostgreSqlTaskResult, Rows>>
      recovery_promise;
  auto recovery_future = recovery_promise.get_future();

  ASSERT_FALSE(task->RequestValue<Rows>(
      R"(SELECT * FROM (
               VALUES
                 (10::integer, E'autocommit\nrecovered', NULL::integer),
                 (11::integer, E'autocommit\nstill usable', 4::integer)
             ) AS t(id, note, score)
             ORDER BY id)"_SQL,
      [&recovery_promise](const bozo::postgresql::PostgreSqlTaskResult &result,
                          const Rows &rows) mutable {
        recovery_promise.set_value({result, rows});
      }));

  io.run();
  io.restart();

  const auto [recovery_result, rows] = recovery_future.get();
  ASSERT_TRUE(recovery_result.Ok());
  EXPECT_EQ(recovery_result.GetState().GetPhase(),
            PostgreSqlTaskPhase::kConnection);
  EXPECT_FALSE(recovery_result.GetState().GetLastTaskError());
  EXPECT_FALSE(recovery_result.GetState().GetLastOzoError());
  EXPECT_TRUE(recovery_result.GetState().GetLastErrorMessage().empty());
  EXPECT_TRUE(recovery_result.GetState().GetLastErrorContext().empty());
  EXPECT_THAT(rows,
              ElementsAre(std::make_tuple(10, "autocommit\nrecovered",
                                          std::nullopt),
                          std::make_tuple(11, "autocommit\nstill usable", 4)));

  const auto close_result = bozo::test::RunTaskOperation(
      io, [&](auto cb) { return task->Close(std::move(cb)); });
  EXPECT_TRUE(close_result.GetState().IsClosed());
}

TEST(PostgreSqlTaskIntegrationTest,
     TransactionFailureStaysInTransactionUntilRollbackAndThenRecovers) {
  const auto conninfo = bozo::test::RequireTestConninfo();
  if (conninfo.empty()) {
    GTEST_SKIP() << "BOZO_PG_TEST_CONNINFO is not set";
  }

  boost::asio::io_context io;
  auto task = MakeDirectFactory(io, conninfo).Create();

  const auto start_result = bozo::test::RunTaskOperation(
      io, [&](auto cb) { return task->StartTransaction(std::move(cb)); });
  ASSERT_TRUE(start_result.Ok());
  ASSERT_TRUE(start_result.GetState().InTransaction());

  const auto transaction_error = bozo::test::RunTaskOperation(io, [&](auto cb) {
    return task->Execute("SELECT 1 / 0;"_SQL, std::move(cb));
  });
  ASSERT_FALSE(transaction_error.Ok());
  EXPECT_FALSE(transaction_error.GetTaskError());
  EXPECT_TRUE(transaction_error.GetOzoError());
  EXPECT_EQ(transaction_error.GetState().GetPhase(),
            PostgreSqlTaskPhase::kTransaction);
  EXPECT_TRUE(transaction_error.GetState().InTransaction());
  EXPECT_FALSE(transaction_error.GetState().GetLastErrorMessage().empty());

  ozo::rows_of<int> blocked_rows;
  const auto blocked_result = bozo::test::RunTaskOperation(io, [&](auto cb) {
    return task->Request("SELECT 7"_SQL, ozo::into(blocked_rows), std::move(cb));
  });
  ASSERT_FALSE(blocked_result.Ok());
  EXPECT_TRUE(blocked_result.GetOzoError());
  EXPECT_EQ(blocked_result.GetState().GetPhase(),
            PostgreSqlTaskPhase::kTransaction);
  EXPECT_TRUE(blocked_result.GetState().InTransaction());
  EXPECT_TRUE(blocked_rows.empty());

  const auto rollback_result = bozo::test::RunTaskOperation(
      io, [&](auto cb) { return task->RollbackTransaction(std::move(cb)); });
  ASSERT_TRUE(rollback_result.Ok());
  EXPECT_EQ(rollback_result.GetState().GetPhase(),
            PostgreSqlTaskPhase::kConnection);
  EXPECT_FALSE(rollback_result.GetState().InTransaction());

  auto bound_rows =
      std::make_shared<ozo::rows_of<int, std::string, std::optional<int>>>();
  std::promise<bozo::postgresql::PostgreSqlTaskResult> recovery_promise;
  auto recovery_future = recovery_promise.get_future();

  ASSERT_FALSE(task->Request(
      R"(SELECT * FROM (
               VALUES
                 (21::integer, E'rollback\nrecovered', NULL::integer),
                 (22::integer, E'rollback\nrecovered\nagain', 8::integer)
             ) AS t(id, note, score)
             ORDER BY id)"_SQL,
      ozo::into(*bound_rows),
      bozo::postgresql::PostgreSqlTask::BindOutput(
          bound_rows,
          [&recovery_promise](
              const bozo::postgresql::PostgreSqlTaskResult &result,
              const ozo::rows_of<int, std::string, std::optional<int>> &)
              mutable { recovery_promise.set_value(result); })));

  io.run();
  io.restart();

  const auto recovery_result = recovery_future.get();
  ASSERT_TRUE(recovery_result.Ok());
  EXPECT_EQ(recovery_result.GetState().GetPhase(),
            PostgreSqlTaskPhase::kConnection);
  EXPECT_FALSE(recovery_result.GetState().GetLastOzoError());
  EXPECT_TRUE(recovery_result.GetState().GetLastErrorMessage().empty());
  EXPECT_THAT(
      *bound_rows,
      ElementsAre(std::make_tuple(21, "rollback\nrecovered", std::nullopt),
                  std::make_tuple(22, "rollback\nrecovered\nagain", 8)));

  const auto close_result = bozo::test::RunTaskOperation(
      io, [&](auto cb) { return task->Close(std::move(cb)); });
  EXPECT_TRUE(close_result.GetState().IsClosed());
  EXPECT_TRUE(task->GetState().IsClosed());
}

TEST(PostgreSqlTaskIntegrationTest,
     FailedConnectionStateRejectsFollowUpWorkUntilClose) {
  boost::asio::io_context io;
  auto task = MakeDirectFactory(io, bozo::test::MakeInvalidConninfo()).Create();

  const auto first_failure = bozo::test::RunTaskOperation(io, [&](auto cb) {
    return task->Execute("SELECT 1"_SQL, std::move(cb));
  });

  ASSERT_FALSE(first_failure.Ok());
  EXPECT_TRUE(first_failure.GetOzoError());
  EXPECT_EQ(first_failure.GetState().GetPhase(), PostgreSqlTaskPhase::kFailed);
  EXPECT_TRUE(task->GetState().IsFailed());

  EXPECT_EQ(task->RequestRawValue(
                "SELECT current_database()"_SQL,
                [](const bozo::postgresql::PostgreSqlTaskResult &,
                   const ozo::result &) {}),
            bozo::postgresql::make_error_code(
                bozo::postgresql::PostgreSqlTaskErrc::kFailed));

  const auto close_result = bozo::test::RunTaskOperation(
      io, [&](auto cb) { return task->Close(std::move(cb)); });
  EXPECT_TRUE(task->GetState().IsClosed());
  EXPECT_TRUE(close_result.GetState().IsClosed());
}

} // namespace
