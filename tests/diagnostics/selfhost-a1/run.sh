#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)

sh "$ROOT/bootstrap/selfhost/check-driver-diagnostics.sh"

printf '%s\n' \
    "PASS: EA101-EA105 preserve stdout, status, primary byte spans, and forbidden artifacts"
