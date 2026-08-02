// Comparison for corpus item 8: a cancellable reactive pipeline with
// bounded demand and an explicit scheduler boundary.
//
// Language: Go. Chosen because #624 cites Go pipelines and explicit
// cancellation (https://go.dev/blog/pipelines) as the evidence to evaluate,
// and because docs/stdlib/stream-protocol.md's "Alternatives considered"
// section rejects exactly this design as Kofun's public contract — while
// keeping channels behind an explicit adapter. So this file is not a
// strawman: it is the alternative the accepted decision argues against,
// written the way Go actually writes it.
//
// Compiled and run by the gate: `go run 08_stream_pipeline.go` prints
// 24, 14, 40 and then `cancelled after 3`.
//
// What to compare — each of the three properties #624 asks for:
//
//   * CANCELLATION is `ctx` threaded by hand through every stage, plus a
//     `select` on `ctx.Done()` in every send. Miss one and the stage leaks a
//     goroutine forever; the compiler does not check it. DD-037 makes
//     `Subscription` affine instead, so dropping it cancels and a double
//     cancel is a type error.
//   * BOUNDED DEMAND is the channel capacity, `demand`. It is a buffer size,
//     not a credit: the producer runs ahead and fills the buffer whether or
//     not the consumer asked. DD-037's `request(n)` is downstream-driven, so
//     the producer never emits more than was requested.
//   * THE SCHEDULER BOUNDARY is `go`. It is one keyword and it is easy to
//     miss in review: nothing in a stage's signature says whether it starts
//     a goroutine. DD-037 requires a `Scheduler` to be an explicit argument
//     precisely so it appears in the type.
//
// Go's version runs today and Kofun's does not, which is the honest headline
// of this row. The rubric scores the specification against this file, and
// says so.

package main

import (
	"context"
	"fmt"
)

// Reading matches the record in 08_stream_pipeline.kofun.
type Reading struct {
	Sensor int
	Value  int
}

// source emits readings until it runs out or the context is cancelled.
// The `defer close` and the `select` on ctx.Done() are both mandatory and
// both are the programmer's responsibility.
func source(ctx context.Context, readings []Reading, demand int) <-chan Reading {
	out := make(chan Reading, demand)
	go func() {
		defer close(out)
		for _, reading := range readings {
			select {
			case out <- reading:
			case <-ctx.Done():
				return
			}
		}
	}()
	return out
}

func filter(ctx context.Context, in <-chan Reading, demand int, keep func(Reading) bool) <-chan Reading {
	out := make(chan Reading, demand)
	go func() {
		defer close(out)
		for reading := range in {
			if !keep(reading) {
				continue
			}
			select {
			case out <- reading:
			case <-ctx.Done():
				return
			}
		}
	}()
	return out
}

func mapValues(ctx context.Context, in <-chan Reading, demand int, transform func(Reading) int) <-chan int {
	out := make(chan int, demand)
	go func() {
		defer close(out)
		for reading := range in {
			select {
			case out <- transform(reading):
			case <-ctx.Done():
				return
			}
		}
	}()
	return out
}

func main() {
	readings := []Reading{
		{Sensor: 1, Value: 12},
		{Sensor: 1, Value: 3},
		{Sensor: 2, Value: 7},
		{Sensor: 2, Value: 20},
		{Sensor: 3, Value: 5},
		{Sensor: 3, Value: 18},
	}

	// Cancelling is the caller's job, and forgetting the defer is the
	// classic leak this pattern is famous for.
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	const demand = 2
	const budget = 3

	values := mapValues(ctx,
		filter(ctx, source(ctx, readings, demand), demand,
			func(reading Reading) bool { return reading.Value > 6 }),
		demand,
		func(reading Reading) int { return reading.Value * 2 })

	// `take(budget)`: there is no operator for it, so it is a counter and an
	// early `cancel()`. The upstream stages find out through ctx.Done().
	taken := 0
	for value := range values {
		fmt.Println(value)
		taken++
		if taken == budget {
			cancel()
			break
		}
	}
	fmt.Printf("cancelled after %d\n", taken)
}
