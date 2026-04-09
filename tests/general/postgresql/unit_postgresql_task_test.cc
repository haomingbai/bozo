#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <boost/asio/io_context.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "ozo/query_builder.h"
#include "postgresql_test_support.h"

namespace {

using bozo::postgresql::make_error_code;
using bozo::postgresql::PostgreSqlTask;
using bozo::postgresql::PostgreSqlTaskErrc;
using bozo::postgresql::PostgreSqlTaskFactory;
using bozo::postgresql::PostgreSqlTaskOptions;
using bozo::postgresql::PostgreSqlTaskPhase;
using ozo::literals::operator""_SQL;
using testing::ElementsAre;

PostgreSqlTaskFactory MakeInvalidFactory(boost::asio::io_context &io) {
  PostgreSqlTaskOptions options;
  options.operation_timeout = std::chrono::milliseconds(250);
  return PostgreSqlTaskFactory(io.get_executor(),
                               bozo::test::MakeInvalidConninfo(), options);
}

TEST(PostgreSqlTaskTest, ErrorCategoryExposesStableMessages) {
  const auto invalid_state = make_error_code(PostgreSqlTaskErrc::kInvalidState);
  const auto closed = make_error_code(PostgreSqlTaskErrc::kClosed);
  const auto failed = make_error_code(PostgreSqlTaskErrc::kFailed);
  const auto cancelled = make_error_code(PostgreSqlTaskErrc::kCancelled);

  EXPECT_EQ(std::string(invalid_state.category().name()),
            "bozo.postgresql.task");
  EXPECT_EQ(invalid_state.message(), "invalid state transition");
  EXPECT_EQ(closed.message(), "task already closed");
  EXPECT_EQ(failed.message(), "task is in failed state");
  EXPECT_EQ(cancelled.message(), "operation cancelled");
}

TEST(PostgreSqlTaskTest, StateSnapshotIsImmutableAndClosePublishesClosedState) {
  boost::asio::io_context io;
  auto task = MakeInvalidFactory(io).Create();

  auto initial_state = task->GetState();
  ASSERT_EQ(initial_state.GetPhase(), PostgreSqlTaskPhase::kCreated);
  EXPECT_FALSE(initial_state.IsClosed());
  EXPECT_FALSE(initial_state.HasConnection());

  auto close_result =
      bozo::test::RunTaskOperation(io, [&](PostgreSqlTask::Callback cb) {
        return task->Close(std::move(cb));
      });

  EXPECT_TRUE(close_result.Ok());
  EXPECT_TRUE(close_result.GetState().IsClosed());
  EXPECT_EQ(task->GetState().GetPhase(), PostgreSqlTaskPhase::kClosed);
  EXPECT_EQ(initial_state.GetPhase(), PostgreSqlTaskPhase::kCreated);
  EXPECT_FALSE(initial_state.IsClosed());
}

TEST(PostgreSqlTaskTest, CommitAndRollbackRequireTransactionState) {
  boost::asio::io_context io;
  auto task = MakeInvalidFactory(io).Create();

  EXPECT_EQ(task->CommitTransaction({}),
            make_error_code(PostgreSqlTaskErrc::kInvalidState));
  EXPECT_EQ(task->RollbackTransaction({}),
            make_error_code(PostgreSqlTaskErrc::kInvalidState));
}

TEST(PostgreSqlTaskTest, SequentialQueuedOperationsCompleteInOrderOnFailure) {
  boost::asio::io_context io;
  auto task = MakeInvalidFactory(io).Create();

  std::mutex mutex;
  std::vector<int> order;
  std::vector<bozo::postgresql::PostgreSqlTaskResult> results;
  std::promise<void> done;
  auto future = done.get_future();
  std::atomic<int> callbacks{0};

  auto callback = [&](int id) {
    return [&, id](const bozo::postgresql::PostgreSqlTaskResult &result) {
      {
        std::lock_guard<std::mutex> lock(mutex);
        order.push_back(id);
        results.push_back(result);
      }
      if (callbacks.fetch_add(1) + 1 == 2) {
        done.set_value();
      }
    };
  };

  ASSERT_FALSE(task->Execute("SELECT 1"_SQL, callback(1)));
  ASSERT_FALSE(task->Execute("SELECT 2"_SQL, callback(2)));

  io.run();
  future.get();

  EXPECT_THAT(order, ElementsAre(1, 2));
  ASSERT_EQ(results.size(), 2U);
  EXPECT_TRUE(results[0].GetOzoError());
  EXPECT_EQ(results[1].GetTaskError(),
            make_error_code(PostgreSqlTaskErrc::kFailed));
  EXPECT_TRUE(task->GetState().IsFailed());
}

TEST(PostgreSqlTaskTest,
     ScheduledStateAllowsQueuedCommitAfterStartTransaction) {
  boost::asio::io_context io;
  auto task = MakeInvalidFactory(io).Create();

  std::mutex mutex;
  std::vector<int> order;
  std::vector<bozo::postgresql::PostgreSqlTaskResult> results;
  std::promise<void> done;
  auto future = done.get_future();
  std::atomic<int> callbacks{0};

  auto callback = [&](int id) {
    return [&, id](const bozo::postgresql::PostgreSqlTaskResult &result) {
      {
        std::lock_guard<std::mutex> lock(mutex);
        order.push_back(id);
        results.push_back(result);
      }
      if (callbacks.fetch_add(1) + 1 == 2) {
        done.set_value();
      }
    };
  };

  ASSERT_FALSE(task->StartTransaction(callback(1)));
  ASSERT_FALSE(task->CommitTransaction(callback(2)));

  io.run();
  future.get();

  EXPECT_THAT(order, ElementsAre(1, 2));
  ASSERT_EQ(results.size(), 2U);
  EXPECT_TRUE(results[0].GetOzoError());
  EXPECT_EQ(results[1].GetTaskError(),
            make_error_code(PostgreSqlTaskErrc::kFailed));
  EXPECT_TRUE(task->GetState().IsFailed());
}

TEST(PostgreSqlTaskTest, CloseCancelsQueuedOperationsAndRejectsNewWork) {
  boost::asio::io_context io;
  auto task = MakeInvalidFactory(io).Create();

  std::mutex mutex;
  std::vector<std::string> events;
  std::promise<void> done;
  auto future = done.get_future();
  std::atomic<int> callbacks{0};

  ASSERT_FALSE(task->Execute(
      "SELECT 1"_SQL,
      [&](const bozo::postgresql::PostgreSqlTaskResult &result) {
        EXPECT_TRUE(result.IsCancelled());
        EXPECT_EQ(result.GetTaskError(),
                  make_error_code(PostgreSqlTaskErrc::kCancelled));
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back("queued");
        if (callbacks.fetch_add(1) + 1 == 2) {
          done.set_value();
        }
      }));

  ASSERT_FALSE(
      task->Close([&](const bozo::postgresql::PostgreSqlTaskResult &result) {
        EXPECT_TRUE(result.Ok());
        EXPECT_TRUE(result.GetState().IsClosed());
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back("close");
        if (callbacks.fetch_add(1) + 1 == 2) {
          done.set_value();
        }
      }));

  io.run();
  future.get();

  EXPECT_THAT(events, ElementsAre("queued", "close"));
  EXPECT_EQ(task->Execute("SELECT 3"_SQL, PostgreSqlTask::Callback{}),
            make_error_code(PostgreSqlTaskErrc::kClosed));
}

TEST(PostgreSqlTaskTest, CloseImmediatelyRejectsAllNewPublicOperations) {
  boost::asio::io_context io;
  auto task = MakeInvalidFactory(io).Create();

  std::promise<void> close_done;
  auto close_future = close_done.get_future();
  ozo::result raw;

  ASSERT_FALSE(task->Close([&](const bozo::postgresql::PostgreSqlTaskResult &result) {
    EXPECT_TRUE(result.Ok());
    EXPECT_TRUE(result.GetState().IsClosed());
    close_done.set_value();
  }));

  EXPECT_EQ(task->Execute("SELECT 1"_SQL, PostgreSqlTask::Callback{}),
            make_error_code(PostgreSqlTaskErrc::kClosed));
  EXPECT_EQ(
      task->Request("SELECT 1"_SQL, ozo::into(raw), PostgreSqlTask::Callback{}),
      make_error_code(PostgreSqlTaskErrc::kClosed));
  EXPECT_EQ(
      task->RequestValue<ozo::rows_of<int>>(
          "SELECT 1"_SQL,
          [](const bozo::postgresql::PostgreSqlTaskResult &,
             const ozo::rows_of<int> &) {}),
      make_error_code(PostgreSqlTaskErrc::kClosed));
  EXPECT_EQ(task->RequestRaw("SELECT 1"_SQL, raw, PostgreSqlTask::Callback{}),
            make_error_code(PostgreSqlTaskErrc::kClosed));
  EXPECT_EQ(
      task->RequestRawValue(
          "SELECT 1"_SQL,
          [](const bozo::postgresql::PostgreSqlTaskResult &, const ozo::result &) {}),
      make_error_code(PostgreSqlTaskErrc::kClosed));
  EXPECT_EQ(task->StartTransaction(PostgreSqlTask::Callback{}),
            make_error_code(PostgreSqlTaskErrc::kClosed));
  EXPECT_EQ(task->CommitTransaction(PostgreSqlTask::Callback{}),
            make_error_code(PostgreSqlTaskErrc::kClosed));
  EXPECT_EQ(task->RollbackTransaction(PostgreSqlTask::Callback{}),
            make_error_code(PostgreSqlTaskErrc::kClosed));
  EXPECT_EQ(task->Close(PostgreSqlTask::Callback{}),
            make_error_code(PostgreSqlTaskErrc::kClosed));

  io.run();
  close_future.get();

  EXPECT_TRUE(task->GetState().IsClosed());
  EXPECT_EQ(task->Execute("SELECT 2"_SQL, PostgreSqlTask::Callback{}),
            make_error_code(PostgreSqlTaskErrc::kClosed));
  EXPECT_EQ(task->StartTransaction(PostgreSqlTask::Callback{}),
            make_error_code(PostgreSqlTaskErrc::kClosed));
}

TEST(PostgreSqlTaskTest, QueueDepthTracksPendingOperationsBeforeIoRuns) {
  boost::asio::io_context io;
  auto task = MakeInvalidFactory(io).Create();

  std::promise<void> done;
  auto future = done.get_future();
  std::atomic<int> callbacks{0};

  auto callback = [&](const bozo::postgresql::PostgreSqlTaskResult &) {
    if (callbacks.fetch_add(1) + 1 == 2) {
      done.set_value();
    }
  };

  ASSERT_FALSE(task->Execute("SELECT 1"_SQL, callback));
  ASSERT_FALSE(task->Execute("SELECT 2"_SQL, callback));

  const auto queued_state = task->GetState();
  EXPECT_EQ(queued_state.GetPhase(), PostgreSqlTaskPhase::kCreated);
  EXPECT_EQ(queued_state.GetQueueDepth(), 2U);

  io.run();
  future.get();

  EXPECT_EQ(task->GetState().GetQueueDepth(), 0U);
  EXPECT_TRUE(task->GetState().IsFailed());
}

TEST(PostgreSqlTaskTest, RequestValueDeliversOutputObjectToCallback) {
  boost::asio::io_context io;
  auto task = MakeInvalidFactory(io).Create();

  std::promise<void> done;
  auto future = done.get_future();
  std::size_t row_count = 42;
  std::optional<bozo::postgresql::PostgreSqlTaskResult> callback_result;

  auto ec = task->RequestValue<ozo::rows_of<int>>(
      "SELECT 1"_SQL,
      [&](const bozo::postgresql::PostgreSqlTaskResult &result,
          const ozo::rows_of<int> &rows) {
        callback_result.emplace(result);
        row_count = rows.size();
        done.set_value();
      });
  ASSERT_FALSE(ec);

  io.run();
  future.get();

  ASSERT_TRUE(callback_result.has_value());
  EXPECT_TRUE(callback_result->GetOzoError());
  EXPECT_EQ(row_count, 0U);
  EXPECT_TRUE(task->GetState().IsFailed());
}

TEST(PostgreSqlTaskTest,
     BindOutputKeepsSharedOutputAliveUntilCallbackCompletes) {
  boost::asio::io_context io;
  auto task = MakeInvalidFactory(io).Create();

  auto rows = std::make_shared<ozo::rows_of<int>>();
  std::weak_ptr<ozo::rows_of<int>> weak_rows = rows;
  std::promise<void> done;
  auto future = done.get_future();
  bool callback_saw_live_rows = false;
  std::size_t row_count = 42;

  auto ec = task->Request(
      "SELECT 1"_SQL, ozo::into(*rows),
      PostgreSqlTask::BindOutput(
          rows,
          [&](const bozo::postgresql::PostgreSqlTaskResult &result,
              const ozo::rows_of<int> &bound_rows) {
            EXPECT_TRUE(result.GetOzoError());
            callback_saw_live_rows = !weak_rows.expired();
            row_count = bound_rows.size();
            done.set_value();
          }));
  ASSERT_FALSE(ec);

  rows.reset();
  io.run();
  future.get();

  EXPECT_TRUE(callback_saw_live_rows);
  EXPECT_EQ(row_count, 0U);
  EXPECT_TRUE(weak_rows.expired());
}

TEST(PostgreSqlTaskTest,
     ScheduledTransactionStateRejectsSecondStartTransactionImmediately) {
  boost::asio::io_context io;
  auto task = MakeInvalidFactory(io).Create();

  ASSERT_FALSE(task->StartTransaction(PostgreSqlTask::Callback{}));
  EXPECT_EQ(task->StartTransaction(PostgreSqlTask::Callback{}),
            make_error_code(PostgreSqlTaskErrc::kInvalidState));

  io.run();
  io.restart();
  EXPECT_TRUE(task->GetState().IsFailed());
}

TEST(PostgreSqlTaskTest, CloseRemainsAvailableAfterFailureAndThenLocksTask) {
  boost::asio::io_context io;
  auto task = MakeInvalidFactory(io).Create();

  const auto failure_result =
      bozo::test::RunTaskOperation(io, [&](PostgreSqlTask::Callback cb) {
        return task->Execute("SELECT 1"_SQL, std::move(cb));
      });
  ASSERT_FALSE(failure_result.Ok());
  ASSERT_TRUE(failure_result.GetOzoError());
  EXPECT_TRUE(task->GetState().IsFailed());

  const auto close_result =
      bozo::test::RunTaskOperation(io, [&](PostgreSqlTask::Callback cb) {
        return task->Close(std::move(cb));
      });
  EXPECT_TRUE(close_result.Ok());
  EXPECT_TRUE(close_result.GetState().IsClosed());
  EXPECT_TRUE(task->GetState().IsClosed());

  EXPECT_EQ(task->Close(PostgreSqlTask::Callback{}),
            make_error_code(PostgreSqlTaskErrc::kClosed));
  EXPECT_EQ(task->StartTransaction(PostgreSqlTask::Callback{}),
            make_error_code(PostgreSqlTaskErrc::kClosed));
}

TEST(PostgreSqlTaskTest, StaticFactoryHelpersCreateTasksInCreatedState) {
  boost::asio::io_context io;

  auto direct_task = PostgreSqlTaskFactory::CreateTask(
      io.get_executor(), bozo::test::MakeInvalidConninfo(), {});
  auto pooled_task = bozo::postgresql::PostgreSqlTaskPoolFactory::CreateTask(
      io.get_executor(), bozo::test::MakeInvalidConninfo(), {});

  ASSERT_NE(direct_task, nullptr);
  ASSERT_NE(pooled_task, nullptr);
  EXPECT_EQ(direct_task->GetState().GetPhase(), PostgreSqlTaskPhase::kCreated);
  EXPECT_EQ(pooled_task->GetState().GetPhase(), PostgreSqlTaskPhase::kCreated);
}

TEST(PostgreSqlTaskTest, ConcurrentEnqueueDeliversAllCallbacksExactlyOnce) {
  boost::asio::io_context io;
  auto task = MakeInvalidFactory(io).Create();

  constexpr int kOperations = 8;
  std::mutex mutex;
  std::vector<int> ids;
  std::promise<void> done;
  auto future = done.get_future();
  std::atomic<int> callbacks{0};
  std::vector<std::thread> threads;
  threads.reserve(kOperations);

  for (int id = 0; id < kOperations; ++id) {
    threads.emplace_back([&, id] {
      auto ec = task->Execute(
          "SELECT 42"_SQL,
          [&, id](const bozo::postgresql::PostgreSqlTaskResult &result) {
            EXPECT_TRUE(result.GetOzoError() || result.GetTaskError());
            {
              std::lock_guard<std::mutex> lock(mutex);
              ids.push_back(id);
            }
            if (callbacks.fetch_add(1) + 1 == kOperations) {
              done.set_value();
            }
          });
      EXPECT_FALSE(ec);
    });
  }

  for (auto &thread : threads) {
    thread.join();
  }

  io.run();
  future.get();

  std::sort(ids.begin(), ids.end());
  ASSERT_EQ(ids.size(), static_cast<std::size_t>(kOperations));
  for (int index = 0; index < kOperations; ++index) {
    EXPECT_EQ(ids[index], index);
  }
  EXPECT_TRUE(task->GetState().IsFailed());
}

} // namespace
