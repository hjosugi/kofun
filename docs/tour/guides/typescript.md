# Coming from TypeScript

## What transfers

`let` bindings, static checks, and a compile step are familiar. Short programs
can infer an obvious type or spell it out.

```text
// TypeScript                     # Kofun
const total: number = laps * 20;  let total: Int = laps * 20
console.log(total);               print(total)
```

## What does not

Kofun `Int` is an exact signed 64-bit integer in this profile, not JavaScript's
floating-point `Number`. Planned ownership checks are language rules rather
than lint conventions.

## Where Kofun is worse today

There is no browser or DOM standard library. The tour host—not Kofun code—turns
printed integers into colour and motion. Text, records, modules, and npm-scale
package choice are not available here.
