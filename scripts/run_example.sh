#!/bin/bash -ex

scripts/build.sh pg docker clang release

docker-compose up -d bozo_postgres

docker-compose run \
    --rm \
    --user "$(id -u):$(id -g)" \
    bozo_build_with_pg_tests \
    bash \
    -exc '/code/scripts/wait_postgres.sh; ${BASE_BUILD_DIR}/clang_release/examples/bozo_request "host=${POSTGRES_HOST} user=${POSTGRES_USER} dbname=${POSTGRES_DB} password=${POSTGRES_PASSWORD}"'

docker-compose run \
    --rm \
    --user "$(id -u):$(id -g)" \
    bozo_build_with_pg_tests \
    bash \
    -exc '/code/scripts/wait_postgres.sh; ${BASE_BUILD_DIR}/clang_release/examples/bozo_connection_pool "host=${POSTGRES_HOST} user=${POSTGRES_USER} dbname=${POSTGRES_DB} password=${POSTGRES_PASSWORD}"'

docker-compose stop bozo_postgres
docker-compose rm -f bozo_postgres
