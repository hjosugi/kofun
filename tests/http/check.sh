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

RESULTS=benchmarks/http/results.json
test -f "$RESULTS" || {
    echo "http integration requires committed benchmark results" >&2
    exit 1
}
grep -Fq '"schema": "kofun.http-benchmark/v1"' "$RESULTS"
grep -Fq '"sample_count": 5' "$RESULTS"
test "$(grep -c 'median_requests_per_second' "$RESULTS")" -eq 2
implementation_commit=$(
    sed -n \
        's/.*"implementation_commit": "\([0-9a-f][0-9a-f]*\)".*/\1/p' \
        "$RESULTS"
)
test "${#implementation_commit}" -eq 40
git cat-file -e "$implementation_commit^{commit}"

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
    "http integration: rejected invalid Kofun route configuration" \
    "http integration: benchmark artifact records five real samples per server"
