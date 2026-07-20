# Coming from Rust

## What transfers

Checked arithmetic goals, affine ownership, explicit mutation, and static
native binaries are common ground.

```text
// Rust                           # Kofun
let total: i64 = laps * 20;       let total: Int = laps * 20
println!("{total}");              print(total)
```

## What does not

Kofun intends to state access as `read`, `edit`, and `take` at boundaries
instead of exposing Rust's borrow and lifetime syntax directly.

## Where Kofun is worse today

The implemented checker is not a Rust alternative. Browser ownership, Text,
records, traits, generics, reclamation, and a mature package ecosystem are all
missing.
