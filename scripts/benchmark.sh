#!/bin/bash -ex

docker-compose build asyncpg aiopg bozo_build_with_pg_tests

if ! [[ "${BOZO_BENCHMARK_COMPILER}" ]]; then
    BOZO_BENCHMARK_COMPILER=clang
fi

if ! [[ "${BOZO_BENCHMARK_BUILD_TYPE}" ]]; then
    BOZO_BENCHMARK_BUILD_TYPE=release
fi

scripts/build.sh pg docker ${BOZO_BENCHMARK_COMPILER} ${BOZO_BENCHMARK_BUILD_TYPE}

function run_benchmark {
    docker-compose run \
        --rm \
        --user "$(id -u):$(id -g)" \
        ${1:?} \
        bash \
        -exc "/code/scripts/wait_postgres.sh; ${2:?} ${3:?}"
}

run_benchmark asyncpg benchmarks/asyncpg_benchmark.py '"postgresql://${POSTGRES_USER}:${POSTGRES_PASSWORD}@${POSTGRES_HOST}/${POSTGRES_DB}"'
run_benchmark aiopg benchmarks/aiopg_benchmark.py '"host=${POSTGRES_HOST} user=${POSTGRES_USER} dbname=${POSTGRES_DB} password=${POSTGRES_PASSWORD}"'
run_benchmark bozo_build_with_pg_tests "\${BASE_BUILD_DIR}/${BOZO_BENCHMARK_COMPILER}_${BOZO_BENCHMARK_BUILD_TYPE}/benchmarks/bozo_benchmark" \
    '"host=${POSTGRES_HOST} user=${POSTGRES_USER} dbname=${POSTGRES_DB} password=${POSTGRES_PASSWORD}"'

function run_bozo_benchmark_performance {
    run_benchmark bozo_build_with_pg_tests "\${BASE_BUILD_DIR}/${BOZO_BENCHMARK_COMPILER}_${BOZO_BENCHMARK_BUILD_TYPE}/benchmarks/bozo_benchmark_performance" \
        "${1:?} --conninfo=\"host=\${POSTGRES_HOST} user=\${POSTGRES_USER} dbname=\${POSTGRES_DB} password=\${POSTGRES_PASSWORD}\""
}

run_bozo_benchmark_performance '--benchmark=reopen_connection --query=simple'
run_bozo_benchmark_performance '--benchmark=reuse_connection --query=simple'
run_bozo_benchmark_performance '--benchmark=use_connection_pool --query=simple --coroutines=1'
run_bozo_benchmark_performance '--benchmark=use_connection_pool --query=simple --coroutines=2'
run_bozo_benchmark_performance '--benchmark=use_connection_pool_mult_threads --query=simple --coroutines=2 --threads=2 --connections=5 --queue=0'
run_bozo_benchmark_performance '--benchmark=use_connection_pool_mult_threads --query=simple --coroutines=2 --threads=2 --connections=2 --queue=4'
run_bozo_benchmark_performance '--benchmark=use_connection_pool --query=simple --coroutines=1 --parse'
run_bozo_benchmark_performance '--benchmark=use_connection_pool --query=complex --coroutines=1'
run_bozo_benchmark_performance '--benchmark=use_connection_pool --query=complex --coroutines=1 --parse'

docker-compose stop bozo_postgres
docker-compose rm -f bozo_postgres
