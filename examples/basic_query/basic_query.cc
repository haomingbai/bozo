#include <boost/asio/io_context.hpp>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "bozo/bozo.h"
#include "ozo/ext/std/optional.h"
#include "ozo/query_builder.h"
#include "ozo/result.h"

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

  std::cout << "initial state: " << PhaseName(task->GetState().GetPhase())
            << '\n';

  int exit_code = EXIT_SUCCESS;

  const auto query =
      R"(SELECT * FROM (
           VALUES
             (1::integer, E'first line\nsecond line'::text, NULL::integer),
             (2::integer, E'another row'::text, 7::integer)
         ) AS t(id, note, score)
         ORDER BY id)"_SQL;

  auto ec =
      task->RequestValue<ozo::rows_of<int, std::string, std::optional<int>>>(
          query,
          [&, task](const PostgreSqlTaskResult &typed_result,
                    const ozo::rows_of<int, std::string, std::optional<int>>
                        &rows) {
        if (PrintFailure("typed request", typed_result)) {
          exit_code = EXIT_FAILURE;
          return;
        }

        std::cout << "typed request phase: "
                  << PhaseName(typed_result.GetState().GetPhase()) << '\n';
        for (const auto &row : rows) {
          std::cout << "row id=" << std::get<0>(row)
                    << " note=" << std::get<1>(row) << " score=";
          if (std::get<2>(row)) {
            std::cout << *std::get<2>(row);
          } else {
            std::cout << "NULL";
          }
          std::cout << '\n';
        }

        auto raw_ec = task->RequestRawValue(
            "SELECT current_database(), current_schema()"_SQL,
            [&, task](const PostgreSqlTaskResult &raw_cb_result,
                      const ozo::result &raw_result) {
              if (PrintFailure("raw request", raw_cb_result)) {
                exit_code = EXIT_FAILURE;
                return;
              }

              std::cout << "raw columns=" << raw_result[0].size()
                        << " database=" << raw_result[0][0].data()
                        << " schema=" << raw_result[0][1].data() << '\n';

              auto close_ec =
                  task->Close([&](const PostgreSqlTaskResult &close_result) {
                    if (PrintFailure("close", close_result)) {
                      exit_code = EXIT_FAILURE;
                      return;
                    }
                    std::cout << "final state: "
                              << PhaseName(close_result.GetState().GetPhase())
                              << '\n';
                  });
              if (close_ec) {
                std::cerr << "failed to schedule close: " << close_ec.message()
                          << '\n';
                exit_code = EXIT_FAILURE;
              }
            });
        if (raw_ec) {
          std::cerr << "failed to schedule raw request: " << raw_ec.message()
                    << '\n';
          exit_code = EXIT_FAILURE;
        }
      });

  if (ec) {
    std::cerr << "failed to schedule typed request: " << ec.message() << '\n';
    return EXIT_FAILURE;
  }

  io.run();
  return exit_code;
}
