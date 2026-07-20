# Coming from Python

## What transfers

Small expressions stay readable, comments begin with `#`, and `//` is floor
division. The browser Core also gives `%` the sign behavior a Python programmer
expects.

```text
# Python                         # Kofun
total = laps * 20                let total = laps * 20
print(total)                     print(total)
```

## What does not

Kofun bindings are statically typed. Full Kofun also intends to make access
mode explicit with `read`, `edit`, and `take` instead of relying on shared
mutable object references.

## Where Kofun is worse today

The ecosystem is tiny. This browser target cannot run strings, lists, loops,
classes, imports, or Python packages, and it does not yet enforce ownership.
