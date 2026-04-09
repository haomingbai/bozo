#include <boost/asio/io_context.hpp>

#include <cstdlib>
#include <functional>
#include <iostream>

#include "bozo/bozo.h"
#include "ozo/query_builder.h"

namespace {

using bozo::postgresql::PostgreSqlTask;
using bozo::postgresql::PostgreSqlTaskPhase;
using bozo::postgresql::PostgreSqlTaskResult;
using ozo::literals::operator""_SQL;

const char *PhaseName(PostgreSqlTaskPhase phase) {
  switch (phase) {
  case PostgreSqlTaskPhase::kCreated:
    return "created";
  case PostgreSqlTaskPhase::kConnection:
    return "connection";
  case PostgreSqlTaskPhase::kTransaction:
    return "transaction";
  case PostgreSqlTaskPhase::kClosed:
    return "closed";
  case PostgreSqlTaskPhase::kFailed:
    return "failed";
  }
  return "unknown";
}

bool PrintFailure(const char *label, const PostgreSqlTaskResult &result) {
  if (result.Ok()) {
    return false;
  }
  std::cerr << label << " failed"
            << " | task_error=" << result.GetTaskError().message()
            << " | ozo_error=" << result.GetOzoError().message()
            << " | state=" << PhaseName(result.GetState().GetPhase())
            << " | error_message=" << result.GetState().GetLastErrorMessage()
            << " | error_context=" << result.GetState().GetLastErrorContext()
            << '\n';
  return true;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <postgres-conninfo>\n";
    return EXIT_FAILURE;
  }

  boost::asio::io_context io;
  bozo::postgresql::PostgreSqlTaskPoolOptions pool_options;
  pool_options.pool_config.capacity = 2;
  pool_options.pool_config.queue_capacity = 2;

  bozo::postgresql::PostgreSqlTaskPoolFactory factory(io.get_executor(),
                                                      argv[1], pool_options);
  auto task_one = factory.Create();
  auto task_two = factory.Create();

  int task_one_pid_first = 0;
  int task_one_pid_second = 0;
  int task_two_pid_first = 0;
  int task_two_pid_second = 0;
  int exit_code = EXIT_SUCCESS;

  std::function<void()> close_tasks;
  std::function<void()> fetch_task_two_second;
  std::function<void()> fetch_task_two_first;
  std::function<void()> fetch_task_one_second;
  std::function<void()> fetch_task_one_first;

  close_tasks = [&] {
    auto close_one = task_one->Close([&](const PostgreSqlTaskResult &result) {
      if (PrintFailure("close task one", result)) {
        exit_code = EXIT_FAILURE;
      }
    });
    auto close_two = task_two->Close([&](const PostgreSqlTaskResult &result) {
      if (PrintFailure("close task two", result)) {
        exit_code = EXIT_FAILURE;
      }
    });
    if (close_one || close_two) {
      std::cerr << "failed to schedule close: " << close_one.message() << " / "
                << close_two.message() << '\n';
      exit_code = EXIT_FAILURE;
    }
  };

  fetch_task_two_second = [&] {
    auto ec = task_two->RequestValue<ozo::rows_of<int>>(
        "SELECT pg_backend_pid()"_SQL,
        [&](const PostgreSqlTaskResult &result, const ozo::rows_of<int> &rows) {
          if (PrintFailure("task two second query", result)) {
            exit_code = EXIT_FAILURE;
            return;
          }
          task_two_pid_second = std::get<0>(rows.front());
          std::cout << "task two pid sequence: " << task_two_pid_first << " -> "
                    << task_two_pid_second << '\n';
          std::cout << "task one pid sequence: " << task_one_pid_first << " -> "
                    << task_one_pid_second << '\n';
          close_tasks();
        });
    if (ec) {
      std::cerr << "failed to schedule task two second query: " << ec.message()
                << '\n';
      exit_code = EXIT_FAILURE;
    }
  };

  fetch_task_two_first = [&] {
    auto ec = task_two->RequestValue<ozo::rows_of<int>>(
        "SELECT pg_backend_pid()"_SQL,
        [&](const PostgreSqlTaskResult &result, const ozo::rows_of<int> &rows) {
          if (PrintFailure("task two first query", result)) {
            exit_code = EXIT_FAILURE;
            return;
          }
          task_two_pid_first = std::get<0>(rows.front());
          fetch_task_two_second();
        });
    if (ec) {
      std::cerr << "failed to schedule task two first query: " << ec.message()
                << '\n';
      exit_code = EXIT_FAILURE;
    }
  };

  fetch_task_one_second = [&] {
    auto ec = task_one->RequestValue<ozo::rows_of<int>>(
        "SELECT pg_backend_pid()"_SQL,
        [&](const PostgreSqlTaskResult &result, const ozo::rows_of<int> &rows) {
          if (PrintFailure("task one second query", result)) {
            exit_code = EXIT_FAILURE;
            return;
          }
          task_one_pid_second = std::get<0>(rows.front());
          fetch_task_two_first();
        });
    if (ec) {
      std::cerr << "failed to schedule task one second query: " << ec.message()
                << '\n';
      exit_code = EXIT_FAILURE;
    }
  };

  fetch_task_one_first = [&] {
    auto ec = task_one->RequestValue<ozo::rows_of<int>>(
        "SELECT pg_backend_pid()"_SQL,
        [&](const PostgreSqlTaskResult &result, const ozo::rows_of<int> &rows) {
          if (PrintFailure("task one first query", result)) {
            exit_code = EXIT_FAILURE;
            return;
          }
          task_one_pid_first = std::get<0>(rows.front());
          fetch_task_one_second();
        });
    if (ec) {
      std::cerr << "failed to schedule task one first query: " << ec.message()
                << '\n';
      exit_code = EXIT_FAILURE;
    }
  };

  fetch_task_one_first();
  io.run();

  if (exit_code == EXIT_SUCCESS) {
    std::cout << "task one kept the same backend connection: "
              << (task_one_pid_first == task_one_pid_second ? "yes" : "no")
              << '\n';
    std::cout << "task two kept the same backend connection: "
              << (task_two_pid_first == task_two_pid_second ? "yes" : "no")
              << '\n';
    std::cout << "task one and task two used different connections: "
              << (task_one_pid_first != task_two_pid_first ? "yes" : "no")
              << '\n';
  }

  return exit_code;
}
