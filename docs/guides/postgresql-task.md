# PostgreSqlTask Guide

This guide is for people using `bozo::postgresql::PostgreSqlTask`.

If you need implementation detail, queue semantics, or state-machine internals,
read the [design document](../design/postgresql-task.md).

## What This Type Is

`bozo::postgresql::PostgreSqlTask` is a thread-safe, callback-based facade over
`ozo` connections and transactions.

It gives you:

- one serialized operation stream per task
- stable public phases through `GetState()`
- direct and pooled factories with the same task API
- request helpers for caller-owned or callback-owned output objects

The task owns its current connection or transaction handle. A pooled task keeps
its acquired pool slot for its lifetime after the first successful acquisition,
so it behaves like a long-lived PostgreSQL session.

## Quick Mental Model

- A fresh task starts in `kCreated`.
- The first query or request that leaves a usable session moves it to
  `kConnection`.
- `StartTransaction()` moves it to `kTransaction`.
- `CommitTransaction()` or `RollbackTransaction()` moves it back to
  `kConnection`.
- `Close()` moves it to `kClosed`.
- Losing the handle moves it to `kFailed`, and only `Close()` remains allowed.

Internally, work is serialized on a strand. You may submit work from multiple
threads, but only one `ozo` operation runs at a time on a task.

## Public Phases

| Phase | Meaning | What is still allowed |
| --- | --- | --- |
| `kCreated` | No connection has been acquired yet. | query/request methods, `StartTransaction()`, `Close()` |
| `kConnection` | The task owns a usable connection in autocommit mode. | query/request methods, `StartTransaction()`, `Close()` |
| `kTransaction` | The task owns a transaction handle. | query/request methods, `CommitTransaction()`, `RollbackTransaction()`, `Close()` |
| `kClosed` | Terminal closed state. | nothing |
| `kFailed` | Terminal unusable-handle state. | `Close()` only |

Unsupported transitions fail synchronously with
`PostgreSqlTaskErrc::kInvalidState`.

## Read State And Errors

`GetState()` returns an immutable snapshot. Useful accessors are:

- `GetPhase()`
- `HasConnection()`
- `InTransaction()`
- `IsClosed()`
- `IsFailed()`
- `GetQueueDepth()`
- `GetLastTaskError()`
- `GetLastOzoError()`
- `GetLastErrorMessage()`
- `GetLastErrorContext()`

Every callback also receives a `PostgreSqlTaskResult`, which contains:

- `Ok()`
- `IsCancelled()`
- `GetTaskError()`
- `GetOzoError()`
- `GetState()`

Use `GetTaskError()` to diagnose local task-policy errors such as closed,
failed, cancelled, or invalid-state cases. Use `GetOzoError()` for PostgreSQL
or transport errors returned by OZO.

## Factories

### Direct Task Factory

```cpp
boost::asio::io_context io;
bozo::postgresql::PostgreSqlTaskFactory factory(
    io.get_executor(),
    "host=127.0.0.1 port=5432 dbname=postgres user=postgres password=postgres");

auto task = factory.Create();
```

### Pool Task Factory

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

## Query Styles

Every `Execute()` / `Request*()` entry point accepts three query forms:

- SQL text plus bound parameters, with the callback as the last argument
- `bozo::postgresql::MakeQuery(...)`
- existing OZO query objects such as `_SQL` and `ozo::query_builder`

If you need move-only parameters such as `std::unique_ptr<std::string>`, use
the bozo parameterized paths (`MakeQuery(...)` or the SQL-text overloads).

### Parameterized Text Overloads

```cpp
task->Execute(
    "INSERT INTO notes (id, note, score) VALUES ($1, $2, $3);",
    1,
    std::string("hello"),
    std::optional<int>{7},
    [](const PostgreSqlTaskResult& result) {
      if (!result.Ok()) {
        return;
      }
    });
```

The callback is always the last argument. Zero-parameter statements still work:

```cpp
task->RequestRawValue(
    "SELECT current_database()",
    [](const PostgreSqlTaskResult& result, const ozo::result& raw) {
      if (!result.Ok()) {
        return;
      }
    });
```

### Explicit bozo Query Objects

```cpp
auto query = bozo::postgresql::MakeQuery(
    "SELECT note FROM notes WHERE id = $1;",
    1);

task->RequestValue<ozo::rows_of<std::string>>(
    std::move(query),
    [](const PostgreSqlTaskResult& result,
       const ozo::rows_of<std::string>& rows) {
      if (!result.Ok()) {
        return;
      }
    });
```

### Caller-Owned Decoded Output

Use `Request()` or `RequestRaw()` when you want to own the output object.

```cpp
auto rows = std::make_shared<ozo::rows_of<int>>();

task->Request(
    "SELECT $1::integer",
    ozo::into(*rows),
    42,
    bozo::postgresql::PostgreSqlTask::BindOutput(
        rows,
        [](const PostgreSqlTaskResult& result,
           const ozo::rows_of<int>& bound_rows) {
          if (!result.Ok()) {
            return;
          }
          // bound_rows is ready here.
        }));
```

The output object must stay alive until the callback fires. `BindOutput()`
exists to make that easy.

### Callback-Owned Decoded Output

Use `RequestValue<Output>()` or `RequestRawValue()` when you do not want to
manage output lifetime yourself.

```cpp
task->RequestValue<
    ozo::rows_of<int, std::string, std::optional<int>>>(
    bozo::postgresql::MakeQuery(
        "SELECT * FROM ("
        "VALUES "
        "($1::integer, $2::text, $3::integer), "
        "($4::integer, $5::text, $6::integer)"
        ") AS t(id, note, score)",
        1,
        std::string("first line\nsecond line"),
        std::optional<int>{},
        2,
        std::string("single line"),
        std::optional<int>{7}),
    [&](const PostgreSqlTaskResult& result,
        const ozo::rows_of<int, std::string, std::optional<int>>& rows) {
      if (!result.Ok()) {
        return;
      }
      // rows is already decoded and ready to read.
    });
```

Constraints:

- `RequestValue<Output>()` requires `Output` to be default-constructible.
- `BindOutput()` requires a non-null `std::shared_ptr`.

### Raw Result Path

```cpp
task->RequestRawValue(
    "SELECT current_database(), current_schema()",
    [](const PostgreSqlTaskResult& result, const ozo::result& raw) {
      if (!result.Ok()) {
        return;
      }
      // raw is ready here.
    });
```

Use this when you want direct access to PostgreSQL's raw result object.

## Transaction Flow

The `transaction-flow` example shows the intended sequence:

1. Create a table on the task-owned connection.
2. Start a transaction.
3. Insert rows and read them inside the transaction.
4. Roll back and verify the rolled-back data is not visible.
5. Start another transaction.
6. Insert and commit.
7. Verify committed visibility on the same session.

## Failure And Recovery Rules

There are three important cases:

### Connection Acquisition Or Handle Failure

If the task cannot obtain or keep a usable connection/transaction handle, it
enters `kFailed`. New work is rejected until you call `Close()`.

### Statement Failure In Connection Mode

If a statement fails while the task is in autocommit mode, the task usually
stays in `kConnection`. A later successful statement clears the stored error
snapshot.

### Statement Failure In Transaction Mode

If a statement fails inside a transaction, PostgreSQL keeps the transaction in
an aborted state. The task still reports `kTransaction`, and follow-up
statements keep failing until `RollbackTransaction()`.

This is why `kFailed` should be read as "the handle is unusable", not merely
"the last SQL statement failed".

## Pool Semantics

The `pool-factory` example demonstrates the intended pooled behavior:

- one task holds one pool connection after first acquisition
- later operations on the same task stay on that connection
- different tasks may hold different backend connections concurrently
- the pool slot is released when the task is closed

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
