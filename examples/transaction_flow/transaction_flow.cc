#include <boost/asio/io_context.hpp>

#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <optional>
#include <string>

#include "bozo/bozo.h"
#include "ozo/ext/std/optional.h"
#include "ozo/query_builder.h"

namespace {

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
  bozo::postgresql::PostgreSqlTaskFactory factory(io.get_executor(), argv[1]);
  auto task = factory.Create();

  int exit_code = EXIT_SUCCESS;
  ozo::rows_of<int, std::string, std::optional<std::string>> rows;
  ozo::rows_of<std::int64_t> count_rows;
  std::function<void()> create_temp_table;
  std::function<void()> create_temp_table_schema;
  std::function<void()> start_rollback_transaction;
  std::function<void()> inspect_after_rollback;
  std::function<void()> start_commit_transaction;
  std::function<void()> inspect_after_commit;
  std::function<void()> finish;

  finish = [&] {
    auto ec = task->Close([&](const PostgreSqlTaskResult &result) {
      if (PrintFailure("close", result)) {
        exit_code = EXIT_FAILURE;
        return;
      }
      std::cout << "final state: " << PhaseName(result.GetState().GetPhase())
                << '\n';
    });
    if (ec) {
      std::cerr << "failed to schedule close: " << ec.message() << '\n';
      exit_code = EXIT_FAILURE;
    }
  };

  inspect_after_commit = [&] {
    count_rows.clear();
    auto ec = task->Request(
        "SELECT COUNT(*) FROM bozo_demo_notes WHERE id >= $1;",
        ozo::into(count_rows), 1,
        [&](const PostgreSqlTaskResult &result) {
          if (PrintFailure("count after commit", result)) {
            exit_code = EXIT_FAILURE;
            return;
          }
          std::cout << "rows visible after commit: "
                    << std::get<0>(count_rows.front()) << '\n';
          finish();
        });
    if (ec) {
      std::cerr << "failed to schedule count after commit: " << ec.message()
                << '\n';
      exit_code = EXIT_FAILURE;
    }
  };

  start_commit_transaction = [&] {
    auto ec = task->StartTransaction([&](const PostgreSqlTaskResult &result) {
      if (PrintFailure("start commit transaction", result)) {
        exit_code = EXIT_FAILURE;
        return;
      }
      std::cout << "phase after second StartTransaction: "
                << PhaseName(result.GetState().GetPhase()) << '\n';
      auto insert_ec = task->Execute(
          "INSERT INTO bozo_demo_notes (id, title, body) "
          "VALUES ($1, $2, $3);",
          3, std::string("committed row"),
          std::string("written\nand committed"),
          [&](const PostgreSqlTaskResult &insert_result) {
            if (PrintFailure("insert committed row", insert_result)) {
              exit_code = EXIT_FAILURE;
              return;
            }
            auto commit_ec = task->CommitTransaction(
                [&](const PostgreSqlTaskResult &commit_result) {
                  if (PrintFailure("commit transaction", commit_result)) {
                    exit_code = EXIT_FAILURE;
                    return;
                  }
                  std::cout << "phase after CommitTransaction: "
                            << PhaseName(commit_result.GetState().GetPhase())
                            << '\n';
                  inspect_after_commit();
                });
            if (commit_ec) {
              std::cerr << "failed to schedule CommitTransaction: "
                        << commit_ec.message() << '\n';
              exit_code = EXIT_FAILURE;
            }
          });
      if (insert_ec) {
        std::cerr << "failed to schedule committed insert: "
                  << insert_ec.message() << '\n';
        exit_code = EXIT_FAILURE;
      }
    });
    if (ec) {
      std::cerr << "failed to schedule second StartTransaction: "
                << ec.message() << '\n';
      exit_code = EXIT_FAILURE;
    }
  };

  inspect_after_rollback = [&] {
    count_rows.clear();
    auto ec = task->Request(
        bozo::postgresql::MakeQuery(
            "SELECT COUNT(*) FROM bozo_demo_notes WHERE id >= $1;", 1),
        ozo::into(count_rows),
        [&](const PostgreSqlTaskResult &result) {
          if (PrintFailure("count after rollback", result)) {
            exit_code = EXIT_FAILURE;
            return;
          }
          std::cout << "rows visible after rollback: "
                    << std::get<0>(count_rows.front()) << '\n';
          start_commit_transaction();
        });
    if (ec) {
      std::cerr << "failed to schedule count after rollback: " << ec.message()
                << '\n';
      exit_code = EXIT_FAILURE;
    }
  };

  start_rollback_transaction = [&] {
    auto ec = task->StartTransaction([&](const PostgreSqlTaskResult &result) {
      if (PrintFailure("start rollback transaction", result)) {
        exit_code = EXIT_FAILURE;
        return;
      }
      std::cout << "phase after StartTransaction: "
                << PhaseName(result.GetState().GetPhase()) << '\n';

      auto insert_ec = task->Execute(
          bozo::postgresql::MakeQuery(
              "INSERT INTO bozo_demo_notes (id, title, body) "
              "VALUES ($1, $2, $3), ($4, $5, $6);",
              1, std::string("rollback row"),
              std::optional<std::string>{}, 2,
              std::string("multiline row"),
              std::optional<std::string>{"first line\nsecond line"}),
          [&](const PostgreSqlTaskResult &insert_result) {
            if (PrintFailure("insert rollback rows", insert_result)) {
              exit_code = EXIT_FAILURE;
              return;
            }

            rows.clear();
            auto select_ec = task->Request(
                "SELECT id, title, body FROM bozo_demo_notes ORDER BY id;"_SQL,
                ozo::into(rows),
                [&](const PostgreSqlTaskResult &select_result) {
                  if (PrintFailure("select inside transaction",
                                   select_result)) {
                    exit_code = EXIT_FAILURE;
                    return;
                  }
                  std::cout << "rows inside transaction:\n";
                  for (const auto &row : rows) {
                    std::cout << "  id=" << std::get<0>(row)
                              << " title=" << std::get<1>(row) << " body=";
                    if (std::get<2>(row)) {
                      std::cout << *std::get<2>(row);
                    } else {
                      std::cout << "NULL";
                    }
                    std::cout << '\n';
                  }

                  auto rollback_ec = task->RollbackTransaction(
                      [&](const PostgreSqlTaskResult &rollback_result) {
                        if (PrintFailure("rollback transaction",
                                         rollback_result)) {
                          exit_code = EXIT_FAILURE;
                          return;
                        }
                        std::cout
                            << "phase after RollbackTransaction: "
                            << PhaseName(rollback_result.GetState().GetPhase())
                            << '\n';
                        inspect_after_rollback();
                      });
                  if (rollback_ec) {
                    std::cerr << "failed to schedule RollbackTransaction: "
                              << rollback_ec.message() << '\n';
                    exit_code = EXIT_FAILURE;
                  }
                });
            if (select_ec) {
              std::cerr << "failed to schedule in-transaction select: "
                        << select_ec.message() << '\n';
              exit_code = EXIT_FAILURE;
            }
          });
      if (insert_ec) {
        std::cerr << "failed to schedule rollback inserts: "
                  << insert_ec.message() << '\n';
        exit_code = EXIT_FAILURE;
      }
    });
    if (ec) {
      std::cerr << "failed to schedule StartTransaction: " << ec.message()
                << '\n';
      exit_code = EXIT_FAILURE;
    }
  };

  create_temp_table = [&] {
    auto ec = task->Execute("DROP TABLE IF EXISTS bozo_demo_notes;"_SQL,
                            [&](const PostgreSqlTaskResult &result) {
                              if (PrintFailure("drop temp table", result)) {
                                exit_code = EXIT_FAILURE;
                                return;
                              }
                              create_temp_table_schema();
                            });
    if (ec) {
      std::cerr << "failed to schedule temp table drop: " << ec.message()
                << '\n';
      exit_code = EXIT_FAILURE;
    }
  };

  create_temp_table_schema = [&] {
    auto ec = task->Execute(
        R"(CREATE TEMP TABLE bozo_demo_notes (
             id integer PRIMARY KEY,
             title text NOT NULL,
             body text
           ))"_SQL,
        [&](const PostgreSqlTaskResult &result) {
          if (PrintFailure("prepare temp table", result)) {
            exit_code = EXIT_FAILURE;
            return;
          }
          std::cout << "phase after table setup: "
                    << PhaseName(result.GetState().GetPhase()) << '\n';
          start_rollback_transaction();
        });
    if (ec) {
      std::cerr << "failed to schedule temp table create: " << ec.message()
                << '\n';
      exit_code = EXIT_FAILURE;
    }
  };

  create_temp_table();
  io.run();
  return exit_code;
}
