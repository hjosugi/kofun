#!/usr/bin/env sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
WORK=${KOFUN_HTTP_BENCH_WORK:-"$ROOT/build/http-benchmark"}
CC=${CC:-cc}
AR=${AR:-ar}
REQUESTS=${REQUESTS:-20000}
CONCURRENCY=${CONCURRENCY:-4}
SAMPLES=${SAMPLES:-5}

test "$(uname -s)" = Linux || {
    printf '%s\n' "http benchmark requires Linux" >&2
    exit 1
}
test "$SAMPLES" -ge 5 || {
    printf '%s\n' "http benchmark requires at least 5 samples" >&2
    exit 2
}
for tool in "$CC" "$AR" git sort sed awk paste; do
    command -v "$tool" >/dev/null 2>&1 || {
        printf '%s\n' "http benchmark requires $tool" >&2
        exit 1
    }
done

mkdir -p "$WORK"
KOFUN_HTTP_BUILD_WORK="$WORK/kofun-build" \
    "$ROOT/framework/http/build.sh" \
    "$ROOT/examples/api_server.kofun" \
    "$WORK/kofun-server" >/dev/null

"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    -I"$ROOT/framework/http/include" \
    "$ROOT/benchmarks/http/minimal_server.c" \
    "$WORK/kofun-build/libkofun_http.a" \
    -o "$WORK/minimal-server"
"$CC" -std=c11 -O2 -Wall -Wextra -Werror \
    "$ROOT/benchmarks/http/load_client.c" \
    -o "$WORK/load-client"

run_server_sample() (
    server=$1
    label=$2
    sample=$3
    stdout="$WORK/$label-$sample.stdout"
    stderr="$WORK/$label-$sample.stderr"
    "$server" >"$stdout" 2>"$stderr" &
    pid=$!
    trap 'kill -KILL "$pid" 2>/dev/null || true' 0 1 2 15

    attempt=0
    while test "$attempt" -lt 200 && test ! -s "$stdout"; do
        sleep 0.01
        attempt=$((attempt + 1))
    done
    ready=$(sed -n '1p' "$stdout")
    case $ready in
        'READY '[1-9][0-9]*) port=${ready#READY } ;;
        *)
            printf '%s\n' "http benchmark server did not become ready" >&2
            exit 1
            ;;
    esac

    if test "$sample" -eq 1; then
        "$WORK/load-client" "$port" 1000 "$CONCURRENCY" >/dev/null
    fi
    elapsed=$(
        "$WORK/load-client" \
            "$port" "$REQUESTS" "$CONCURRENCY"
    )
    printf '%s\n' "$elapsed" >>"$WORK/$label.samples-ns"

    kill -TERM "$pid"
    wait "$pid"
    trap - 0 1 2 15
    test ! -s "$stderr"
    test "$(sed -n '2p' "$stdout")" = DRAINING
    test "$(sed -n '3p' "$stdout")" = ""
)

: >"$WORK/kofun.samples-ns"
: >"$WORK/minimal.samples-ns"
sample=1
while test "$sample" -le "$SAMPLES"; do
    run_server_sample "$WORK/kofun-server" kofun "$sample"
    run_server_sample "$WORK/minimal-server" minimal "$sample"
    sample=$((sample + 1))
done

middle=$((SAMPLES / 2 + 1))
kofun_median=$(sort -n "$WORK/kofun.samples-ns" | sed -n "${middle}p")
minimal_median=$(sort -n "$WORK/minimal.samples-ns" | sed -n "${middle}p")
kofun_rps=$(awk \
    -v requests="$REQUESTS" -v elapsed="$kofun_median" \
    'BEGIN { printf "%.2f", requests * 1000000000 / elapsed }')
minimal_rps=$(awk \
    -v requests="$REQUESTS" -v elapsed="$minimal_median" \
    'BEGIN { printf "%.2f", requests * 1000000000 / elapsed }')

printf '%s\n' \
    "implementation_commit=$(git -C "$ROOT" rev-parse HEAD)" \
    "requests=$REQUESTS" \
    "concurrency=$CONCURRENCY" \
    "keep_alive=true" \
    "samples=$SAMPLES" \
    "kofun_samples_ns=$(paste -sd, "$WORK/kofun.samples-ns")" \
    "minimal_samples_ns=$(paste -sd, "$WORK/minimal.samples-ns")" \
    "kofun_median_ns=$kofun_median" \
    "minimal_median_ns=$minimal_median" \
    "kofun_requests_per_second=$kofun_rps" \
    "minimal_requests_per_second=$minimal_rps"
