# Coding interview profile

## Objective

The goal of the Kofun interview profile is to let you concentrate on explaining the algorithm rather than on language ceremony.

## Standard names

```text
List[T]
Map[K, V]
Set[T]
Deque[T]
Queue[T]
Stack[T]
Heap[T]
PriorityQueue[T]
UnionFind
Graph[T]
```

In the MVP, only List and some persistent-style helpers are implemented. The rest is part of the 13,500-item backlog.

## Required operations

```text
len(value)
sort(values)
reverse(values)
contains(container, value)
first(values)
last(values)
push(values, value)
range(start, end)
enumerate(values)
zip(left, right)
```

## Binary search

```kofun
fn binary_search(values: List[Int], target: Int) -> Int {
    let mut left = 0
    let mut right = len(values) - 1

    while left <= right {
        let middle = left + (right - left) // 2
        let value = values[middle]

        if value == target {
            return middle
        } else if value < target {
            left = middle + 1
        } else {
            right = middle - 1
        }
    }
    return -1
}
```

## Functional alternative

```kofun
fn squares_of_even(values: List[Int]) -> List[Int] {
    return values
        |> filter(fn(x) => x % 2 == 0)
        |> map(fn(x) => x * x)
}
```

## Why GC is useful here

Ordinary interview data structures are GC-managed.

- no lifetime annotations on linked list nodes
- shared nodes in graph adjacency are easy to work with
- no need to explain ownership splitting for tree recursion
- you can concentrate on algorithmic complexity

Only resources such as files, sockets, and locks use ownership contracts.

## Complexity documentation

The standard library reference shows the following for each operation.

```text
Time: amortized O(1)
Space: O(1) additional
Invalidation: none
Mutation: returns a new persistent value / edits exclusively
```

## Interview mode tooling

planned:

```bash
kofun interview new binary-search
kofun interview test solution.kofun
kofun interview bench solution.kofun
```

constraints:

- no network required
- solvable with the standard library alone
- fast startup
- deterministic output
- no hidden magic imports

## Google-style interview relevance

There is no guarantee that Kofun itself is permitted in a Google interview. In an actual interview you need to confirm which languages are allowed.

The value of this profile lies in a language design that expresses algorithms clearly, and in building thinking habits that port easily to Python/Rust/Java/C++.
