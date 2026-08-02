#!/bin/sh
# A stand-in for clang that never answers (#901).
#
# `kofun bindgen-c --clang PATH` takes the compiler as a path, so pointing it
# here is how the gate exercises the wall-clock bound on a clang subprocess
# without needing a header that makes the real clang diverge. The tool must
# give up on its own, name the bound, and write nothing.
#
# `exec` matters: the process the tool spawns has to *be* the sleep, so the
# tool's SIGKILL reaps it. A shell that forked a sleep would leave the sleep
# orphaned in CI. The sleep is longer than the tool's bound and short enough
# that a missed kill still cannot outlive the gate.
exec sleep 30
