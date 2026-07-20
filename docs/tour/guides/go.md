# Coming from Go

## What transfers

Small complete programs, explicit `main`, simple tooling, and direct
compilation are shared priorities.

```text
// Go                        # Kofun
total := laps * 20           let total = laps * 20
fmt.Println(total)           print(total)
```

## What does not

Kofun's planned ownership vocabulary says `read`, `edit`, or `take` where
access crosses a boundary. In this arithmetic Core, `/` truncates but `//`
uses mathematical floor division.

## Where Kofun is worse today

Kofun has no production goroutines, standard library, package ecosystem, or Go
1-style compatibility promise. The browser slice only executes Int arithmetic.
