# bozo

`bozo` is a standalone PostgreSQL task facade built on top of `bsrvcore` and
the latest `haomingbai/ozo`.

It ships as a normal CMake library package:

- package name: `bozo`
- imported target: `bozo::bozo`
- public namespace: `bozo::postgresql`

`bozo` targets `haomingbai/ozo` commit
`b44bdadb1c6bc705a326c26ac6009f1f864c9377`, which no longer requires TS
executors.

For query output you can choose either:

- caller-owned output via `Request()` / `RequestRaw()`
- callback-owned output via `RequestValue()` / `RequestRawValue()`

## Dependency Resolution

`bozo` resolves dependencies in this order:

1. Existing CMake targets `bsrvcore::bsrvcore` and `yandex::ozo`
2. Local config packages via `find_package(bsrvcore CONFIG)` and `find_package(ozo CONFIG)`
3. Explicit local source trees via `BOZO_BSRVCORE_SOURCE_DIR` and `BOZO_OZO_SOURCE_DIR`
4. Network fetch via `FetchContent`

Examples:

```bash
cmake -S . -B build \
  -DBOZO_BSRVCORE_CMAKE_DIR=/path/to/bsrvcore/build \
  -DBOZO_OZO_CMAKE_DIR=/path/to/ozo/build
```

```bash
cmake -S . -B build \
  -DBOZO_BSRVCORE_SOURCE_DIR=/path/to/bsrvcore \
  -DBOZO_OZO_SOURCE_DIR=/path/to/ozo
```

```bash
cmake -S . -B build
```

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Install

```bash
cmake --install build --prefix /tmp/bozo-install
```

## Tests

Unit tests:

```bash
cmake -S . -B build-test \
  -DBOZO_BUILD_TESTS=ON \
  -DBOZO_BUILD_EXAMPLES=OFF
cmake --build build-test --parallel
ctest --test-dir build-test --output-on-failure
```

Integration tests against an existing PostgreSQL instance:

```bash
cmake -S . -B build-it \
  -DBOZO_BUILD_TESTS=ON \
  -DBOZO_BUILD_INTEGRATION_TESTS=ON \
  -DBOZO_BUILD_EXAMPLES=OFF \
  -DBOZO_INTEGRATION_TEST_CONNINFO="host=127.0.0.1 port=5432 dbname=postgres user=postgres password=postgres"
cmake --build build-it --parallel
ctest --test-dir build-it --output-on-failure -L integration
```

Integration tests with the bundled PostgreSQL container fixture:

```bash
cmake -S . -B build-it \
  -DBOZO_BUILD_TESTS=ON \
  -DBOZO_BUILD_INTEGRATION_TESTS=ON \
  -DBOZO_BUILD_EXAMPLES=OFF
cmake --build build-it --parallel
ctest --test-dir build-it --output-on-failure -L integration
```

The container fixture uses `docker` or `podman`, binds PostgreSQL to
`127.0.0.1:${BOZO_INTEGRATION_TEST_HOST_PORT}`, and sets
`BOZO_PG_TEST_CONNINFO` automatically for the test process.

## Examples

Three example programs are installed into `build/bin/examples`:

- `basic-query`
- `transaction-flow`
- `pool-factory`

Each example expects a PostgreSQL conninfo string:

```bash
./build/bin/examples/basic-query \
  "host=127.0.0.1 port=5432 dbname=postgres user=postgres password=postgres"
```

See [docs/manual/postgresql-task.md](docs/manual/postgresql-task.md).
