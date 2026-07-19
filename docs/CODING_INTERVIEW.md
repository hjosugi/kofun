# Coding interview profile

## Objective

Frostのinterview profileは、language ceremonyではなくalgorithmの説明に集中できることを目標にする。

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

MVPではListと一部persistent-style helperのみ実装済み。残りは13,500件backlogに含む。

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

```frost
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

```frost
fn squares_of_even(values: List[Int]) -> List[Int] {
    return values
        |> filter(fn(x) => x % 2 == 0)
        |> map(fn(x) => x * x)
}
```

## Why GC is useful here

ordinary interview data structuresはGC-managedにする。

- linked list nodeでlifetime annotation不要
- graph adjacencyでshared nodeを扱いやすい
- tree recursionでownership splitを説明する必要がない
- algorithm complexityに集中できる

file、socket、lockのようなresourceだけownership contractを使う。

## Complexity documentation

standard library referenceは各operationに次を表示する。

```text
Time: amortized O(1)
Space: O(1) additional
Invalidation: none
Mutation: returns a new persistent value / edits exclusively
```

## Interview mode tooling

planned:

```bash
frost interview new binary-search
frost interview test solution.frost
frost interview bench solution.frost
```

constraints:

- network不要
- standard libraryだけで解ける
- startupが速い
- deterministic output
- hidden magic importなし

## Google-style interview relevance

Frost自体がGoogle interviewで許可される保証はない。実際のinterviewでは許可言語を確認する必要がある。

このprofileの価値は、algorithmを明確に表現するlanguage designと、Python/Rust/Java/C++へ移植しやすい思考習慣を作ることにある。
