# PostgreSqlTask Manual

## Overview

`bozo::postgresql::PostgreSqlTask` is a thread-safe, callback-based facade over
`ozo` connections and transactions.

It provides:

- direct connection tasks via `PostgreSqlTaskFactory`
- pooled connection tasks via `PostgreSqlTaskPoolFactory`
- explicit connection and transaction state transitions
- serialized user operations through an internal thread-safe queue
- immutable state snapshots via `GetState()`

The task itself owns the underlying connection handle. A pooled task keeps one
pool slot for its whole lifetime after the first successful request, so it
behaves like a long-lived session.

## Executors

`bozo` uses `bsrvcore::IoContextExecutor`, which maps to
`boost::asio::io_context::executor_type`. No TS executor compatibility layer is
required for the targeted `haomingbai/ozo` revision.

## State Model

Visible task phases:

- `kCreated`
- `kConnection`
- `kTransaction`
- `kClosed`
- `kFailed`

Read state through `PostgreSqlTaskState` getters:

- `GetPhase()`
- `HasConnection()`
- `InTransaction()`
- `IsClosed()`
- `IsFailed()`
- `GetLastTaskError()`
- `GetLastOzoError()`
- `GetLastErrorMessage()`
- `GetLastErrorContext()`
- `GetQueueDepth()`

`GetState()` returns a snapshot. Callers cannot mutate internal task state.

## Operation Ordering

Every user operation is enqueued and executed in submission order. Internal work
is serialized on a strand, and callbacks are posted through a callback strand.

This guarantees:

- no concurrent `ozo` operations on the same task
- stable callback ordering for sequential submissions
- safe submission from multiple threads

If the task enters `kFailed`, queued operations that have not started are
completed with a local `kFailed` error.

If the task is closed, queued operations that have not started are completed
with `kCancelled` plus `boost::asio::error::operation_aborted`.

## State Transitions

Supported transitions:

- `Execute()` / `Request()` / `RequestValue()` / `RequestRaw()` / `RequestRawValue()`:
  - `kCreated -> kConnection`
  - `kConnection -> kConnection`
  - `kTransaction -> kTransaction`
- `StartTransaction()`:
  - `kCreated -> kTransaction`
  - `kConnection -> kTransaction`
- `CommitTransaction()`:
  - `kTransaction -> kConnection`
- `RollbackTransaction()`:
  - `kTransaction -> kConnection`
- `Close()`:
  - any non-terminal state -> `kClosed`

Unsupported transitions are rejected immediately with `PostgreSqlTaskErrc::kInvalidState`.

## Failure Behavior

`bozo` distinguishes three failure classes:

- connection acquisition failure:
  the task enters `kFailed`, and new work is rejected until `Close()`
- statement failure in autocommit / connection mode:
  the task stays in `kConnection`, and a later valid statement can clear the
  stored error snapshot
- statement failure inside a transaction:
  the task stays in `kTransaction`; PostgreSQL marks the transaction aborted,
  and later statements keep failing until `RollbackTransaction()`

This means SQL errors do not automatically turn every task into terminal
`kFailed`. Terminal failure is reserved for losing the connection or receiving
an unusable handle back from OZO.

## Output Readiness And Lifetime

`PostgreSqlTask::Request()` and `RequestRaw()` follow `ozo` output lifetime
rules:

- the caller owns the output object
- the output object must stay alive until the callback fires

The output is ready to read when the callback fires. `ozo::request()` decodes
rows into `out` first and only then completes the asynchronous operation.

This is why examples that use `Request()` keep `rows`, `result`, or other
output containers outside the callback initiation expression.

If you do not want to manage output lifetime yourself, use the callback-owned
forms:

- `RequestValue<Output>()`
- `RequestRawValue()`

They allocate the output object inside `bozo` and pass it into the callback
after decoding is complete.

Constraints:

- `RequestValue<Output>()` requires `Output` to be default-constructible
- `BindOutput()` requires a non-null `std::shared_ptr`

## Direct Factory

```cpp
boost::asio::io_context io;
bozo::postgresql::PostgreSqlTaskFactory factory(
    io.get_executor(),
    "host=127.0.0.1 port=5432 dbname=postgres user=postgres password=postgres");

auto task = factory.Create();
```

## Pool Factory

```cpp
boost::asio::io_context io;
bozo::postgresql::PostgreSqlTaskPoolOptions options;
options.pool_config.capacity = 4;
options.pool_config.queue_capacity = 4;

bozo::postgresql::PostgreSqlTaskPoolFactory factory(
    io.get_executor(),
    "host=127.0.0.1 port=5432 dbname=postgres user=postgres password=postgres",
    options);

auto task = factory.Create();
```

## Typed Request Example

```cpp
task->RequestValue<
    ozo::rows_of<int, std::string, std::optional<int>>>(
    R"(SELECT * FROM (
         VALUES
           (1::integer, E'first line\nsecond line'::text, NULL::integer),
           (2::integer, 'single line'::text, 7::integer)
       ) AS t(id, note, score))"_SQL,
    [&](const PostgreSqlTaskResult& result,
        const ozo::rows_of<int, std::string, std::optional<int>>& rows) {
      if (!result.Ok()) {
        return;
      }
      // rows now contains two rows and three columns.
    });
```

## Bound Output Example

```cpp
auto rows = std::make_shared<ozo::rows_of<int>>();

task->Request(
    "SELECT pg_backend_pid()"_SQL,
    ozo::into(*rows),
    bozo::postgresql::PostgreSqlTask::BindOutput(
        rows,
        [](const PostgreSqlTaskResult& result,
           const ozo::rows_of<int>& bound_rows) {
          if (!result.Ok()) {
            return;
          }
          // bound_rows is ready here and rows stayed alive via shared_ptr.
        }));
```

## Row Shape Notes

The current public `bozo` API is optimized for built-in PostgreSQL row shapes
such as:

- `ozo::rows_of<int, std::string, std::optional<int>>`
- `ozo::result`

Custom OID-map injection for composite user-defined PostgreSQL types is not
exposed in this revision, so `BOOST_HANA_ADAPT_STRUCT`-based composite decoding
is intentionally left out of the public examples and tests.

## Raw Result Example

```cpp
task->RequestRawValue(
    "SELECT current_database(), current_schema()"_SQL,
    [](const PostgreSqlTaskResult& result, const ozo::result& raw) {
      if (!result.Ok()) {
        return;
      }
      // raw is ready here.
    });
```

This path is useful when you need direct access to the raw PostgreSQL result for
custom decoding or debugging.

## Transaction Flow Example

The `transaction-flow` example shows this sequence:

1. Create a temp table on a task-owned connection
2. Start a transaction
3. Insert multiple rows, including multiline text and `NULL`
4. Read rows inside the transaction
5. Roll back and verify the table is empty
6. Start another transaction
7. Insert and commit
8. Verify committed visibility on the same connection

## Pool Semantics

The `pool-factory` example demonstrates that two pooled tasks can keep two
distinct backend PIDs and then reuse the same PID on later requests. This is
the intended behavior:

- one task maps to one held connection after first acquisition
- later operations on the same task stay on that connection
- different tasks can hold different pool connections concurrently

## Running Examples

```bash
./build/bin/examples/basic-query \
  "host=127.0.0.1 port=5432 dbname=postgres user=postgres password=postgres"
```

```bash
./build/bin/examples/transaction-flow \
  "host=127.0.0.1 port=5432 dbname=postgres user=postgres password=postgres"
```

```bash
./build/bin/examples/pool-factory \
  "host=127.0.0.1 port=5432 dbname=postgres user=postgres password=postgres"
```

## Test Coverage

The shipped tests cover:

- immutable state snapshots
- invalid transaction transitions
- sequential callback ordering
- multi-threaded enqueue behavior
- close cancellation semantics
- multi-row and multi-column requests
- `std::optional` and multiline text
- raw `ozo::result` usage
- autocommit syntax-error recovery
- transaction-aborted state until rollback
- terminal failed-state rejection after connect failure
- transaction commit and rollback visibility
- pooled task connection retention
