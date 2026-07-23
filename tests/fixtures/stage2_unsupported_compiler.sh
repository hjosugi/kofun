#!/usr/bin/env sh
set -eu

if test "$#" -eq 5 && test "$1" = --compile-outcome; then
    printf '%s\n' \
        'error[E2S10]: simulated unsupported Stage 2 lowering at byte 0'
    exit 3
fi

printf '%s\n' 'simulated Stage 2 compiler: unsupported invocation' >&2
exit 2
