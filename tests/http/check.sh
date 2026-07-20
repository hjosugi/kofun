#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"

if [ "$(uname -s)" != "Linux" ]; then
    echo "http integration requires Linux" >&2
    exit 1
fi

for tool in cc ar; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "http integration requires $tool" >&2
        exit 1
    fi
done

mkdir -p build
./framework/http/build.sh examples/api_server.kofun build/http-api-test
./framework/http/build.sh \
    tests/http/invalid_route.kofun \
    build/http-invalid-route-test
cc -std=c11 -O2 -Wall -Wextra -Werror \
    tests/http/http_integration.c \
    -o build/http-integration-check
./build/http-integration-check ./build/http-api-test

set +e
./build/http-invalid-route-test \
    >build/http-invalid-route.stdout \
    2>build/http-invalid-route.stderr
invalid_route_status=$?
set -e
test "$invalid_route_status" -eq 2
test ! -s build/http-invalid-route.stdout
test ! -s build/http-invalid-route.stderr

printf '%s\n' \
    "http integration: rejected invalid Kofun route configuration"
