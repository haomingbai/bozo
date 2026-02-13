# OZO Asio Migration (Boost 1.74+)

## Minimum versions

- **Boost**: `>= 1.74` (uses Boost.Asio 1.18 executor model by default)
- **Boost.Asio**: from Boost 1.74+
- **resource_pool**: migrated version (included as submodule in this fork)

## Behavior and compatibility changes

1. **Default executor model switched to modern Asio executors**
- OZO no longer forces `BOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT`.
- By default, build uses the non-TS executor model introduced in Boost 1.74.

2. **Legacy TS executor remains opt-in**
- To keep old behavior, configure with:
  - `-DOZO_USE_TS_EXECUTOR=ON`

3. **Completion token initiation path updated**
- OZO now uses `boost::asio::async_initiate` directly.
- This resolves ADL ambiguity with `async_initiate` on newer Asio.

4. **libpq status handling extended**
- Newer libpq statuses (`PGRES_PIPELINE_*`, `PGRES_TUPLES_CHUNK`) are handled when available.

## Notes for tests/examples

- PG integration tests require a PostgreSQL instance reachable via `OZO_PG_TEST_CONNINFO`.
- `ltree` integration cases require the server to provide the `ltree` extension (usually via contrib packages).
