#!/bin/sh
set -eu

action="${1:-}"
if [ -z "$action" ]; then
  echo "usage: $0 <start|stop>" >&2
  exit 1
fi

runtime="${BOZO_CONTAINER_RUNTIME:-}"
if [ -z "$runtime" ]; then
  if command -v docker >/dev/null 2>&1; then
    runtime="docker"
  elif command -v podman >/dev/null 2>&1; then
    runtime="podman"
  else
    echo "no supported container runtime found" >&2
    exit 1
  fi
fi

image="${BOZO_CONTAINER_IMAGE:-postgres:16-alpine}"
name="${BOZO_CONTAINER_NAME:-bozo-postgres-test}"
host_port="${BOZO_CONTAINER_HOST_PORT:-55432}"
db="${BOZO_CONTAINER_DB:-bozo_test}"
user="${BOZO_CONTAINER_USER:-bozo}"
password="${BOZO_CONTAINER_PASSWORD:-bozo}"

case "$action" in
  start)
    "$runtime" rm -f "$name" >/dev/null 2>&1 || true
    "$runtime" run -d \
      --name "$name" \
      -e POSTGRES_DB="$db" \
      -e POSTGRES_USER="$user" \
      -e POSTGRES_PASSWORD="$password" \
      -p "127.0.0.1:${host_port}:5432" \
      "$image" >/dev/null

    attempts=0
    until "$runtime" exec "$name" pg_isready -U "$user" -d "$db" >/dev/null 2>&1
    do
      attempts=$((attempts + 1))
      if [ "$attempts" -ge 60 ]; then
        "$runtime" logs "$name" >&2 || true
        echo "postgres container did not become ready in time" >&2
        exit 1
      fi
      sleep 1
    done
    ;;
  stop)
    "$runtime" rm -f "$name" >/dev/null 2>&1 || true
    ;;
  *)
    echo "usage: $0 <start|stop>" >&2
    exit 1
    ;;
esac
