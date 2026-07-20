# Memory model

## 1. Goals

Kofun's memory model aims to satisfy all of the following at once.

- ordinary application code can be written as if in a GC language
- files, sockets, locks, transactions, and GPU buffers can be released
  deterministically
- use-after-free, double free, and data races are prevented in safe code
- no lifetime parameters are written in everyday code
- the compiler can detect unique values and reuse them in place
- a no-GC profile can be reached for embedded, real-time, and
  high-performance use

## 2. Three memory domains

### 2.1 Copy values

The initial closed Copy set is `Int`, `Float`, `Bool`, and `Unit`. Copy is not
user-implementable. Tuples and records remain non-Copy until a later
type-directed derivation decision.

```kofun
let a = 42
let b = a
print(a)
print(b)
```

A copy does not require an explicit heap allocation.

### 2.2 Managed values

`Text`, an ordinary `List[T]`, records, closures, graph data, and the like can
live on the managed heap.

```kofun
let names = ["A", "B", "C"]
let alias = names
```

Ordinary managed values are reclaimed by the GC. The language surface is
immutable by default, so even with aliases, data races and unexpected mutation
are unlikely.

The compiler is free to apply any of the following optimizations.

- stack allocation
- scalar replacement
- arena allocation
- reference counting
- tracing GC
- promotion to owned allocation
- in-place reuse

It must not change the observable semantics, however.

### 2.3 Owned resources

An external resource, or a value that needs deterministic cleanup, is bound
with `own`.

```kofun
let own file = File.open("data.csv")
```

Owned values are affine.

- can be consumed zero times or once
- cannot be consumed twice
- automatically dropped at end of scope if not consumed
- the original binding cannot be used after `take`

The reason for affine rather than linear is that scope cleanup can then safely
handle early returns and unused resources.

## 3. Parameter modes

### 3.1 `read T`

A read-only, non-owning view.

```kofun
fn checksum(read bytes: Bytes) -> Int {
    # bytes cannot be mutated or consumed here
}
```

Properties:

- multiple `read` views can be held at the same time
- does not consume the original value
- in v1, a view cannot escape the function
- the compiler infers the lifetime from the lexical scope

### 3.2 `edit T`

An exclusive mutable view.

```kofun
fn normalize(edit values: Array[Float]) {
    # exclusive mutation is allowed
}
```

Properties:

- no other `read` or `edit` view can be created for the same period
- does not consume the original value
- non-escaping in v1
- the mutation is recorded as an effect

### 3.3 `take T`

Ownership transfer.

```kofun
fn send(take socket: Socket, read payload: Bytes) {
    # socket is owned by this call
}
```

Call site:

```kofun
let own socket = Socket.connect(address)
send(socket, payload)

# compile error: socket was taken
print(socket.peer())
```

Whether `take` must also be written at the call site will be decided by UX
testing. The initial proposal puts it only on the parameter declaration and
makes the ownership transfer explicit through a compiler diagnostic. A proposal
to allow the call-site annotation `send(take socket, payload)` for APIs that
need close review is also in the backlog.

## 4. `let own`

```kofun
let own file = File.open(path)
```

This binding has the following state machine.

```text
uninitialized
    -> live
    -> taken
    -> dropped
```

Forbidden transitions:

```text
taken -> read
taken -> edit
taken -> take
dropped -> any use
live + active edit -> another read/edit
live + active read -> edit/take
```

## 5. Branches

```kofun
let own socket = connect()

if should_send {
    send(socket)
} else {
    close(socket)
}
```

Because it is consumed in both branches, `socket` cannot be used after the
branch.

Even when only one branch consumes it, the conservative v1 checker forbids use
after the branch.

```kofun
if should_send {
    send(socket)
}

# compile error in v1: socket may have been taken
```

Later, state refinement will allow a boolean condition to be related to the
resource state.

## 6. Loops

When an outer owned value is consumed inside a loop, the loop may run zero
times or multiple times.

```kofun
let own socket = connect()

while condition {
    send(socket) # rejected
}
```

The safe form:

```kofun
let mut pending: Socket? = connect()

while condition && pending != null {
    let own socket = pending.take()
    send(socket)
    pending = null
}
```

A better state API will be provided once ADTs and pattern matching are
implemented.

## 7. Closures

Closure capture is classified into 3 kinds.

```kofun
fn make_reader(read data: Bytes) -> fn() -> Int
fn make_editor(edit data: Buffer) -> fn() -> Void
fn make_owner(take data: Resource) -> fn() -> Void
```

v1 rules:

- a `read` / `edit` capture cannot go into an escaping closure
- an escaping closure can only capture managed values or taken owned values
- values passed to an async task satisfy a `Send`-equivalent auto trait
- values shared between threads satisfy a `Share` equivalent

## 8. GC design

The production runtime is expected to default to a generational precise
tracing GC.

### Nursery

- thread-local bump allocation
- small managed objects
- copying minor collection
- precise stack map

### Old generation

- compacting or region-based collector
- large object space
- optional concurrent marking
- pinned object support

### Compiler cooperation

- safepoint insertion
- exact root map
- write barrier insertion and elimination
- escape analysis
- object layout metadata
- ownership-based allocation avoidance

### Operational controls

```text
KOFUN_GC_NURSERY_MB
KOFUN_GC_MAX_HEAP_MB
KOFUN_GC_PAUSE_TARGET_MS
KOFUN_GC_LOG
```

The names are not final; in the production API they will be integrated into
the manifest and CLI config.

## 9. Owned-to-managed conversion

A resource wrapper meant to be shared for a long time is explicitly `share`d.

```kofun
let own client = Client.connect(endpoint)
let shared = share(client)
```

After `share`:

- the original owned binding is taken
- the shared handle can be managed by the GC or by atomic reference counting
- if a deterministic close is needed, follow the `Shared[Client]` protocol
- do not make correctness depend on finalizers alone

## 10. Finalizers

A GC finalizer is last-resort cleanup and is not used in the normal resource
protocol.

Designs that are forbidden:

- leaving a transaction commit to a finalizer
- leaving lock release timing to a finalizer
- leaving the correctness of a file flush to a finalizer

Resources are handled by scope cleanup, `take`, or a `with`-equivalent
resource scope.

## 11. Unsafe boundary

Operations that fall outside the safe language core are separated from ordinary
modules.

Planned example:

```kofun
import trusted.memory

trusted fn from_raw_pointer[T](ptr: Ptr[T], len: Int) -> Slice[T]
```

Principles:

- do not scatter `unsafe` around as a short escape hatch
- a trusted module exposes its preconditions and postconditions through types
  and contracts
- a linter measures the trusted surface area
- unsafe capabilities are recorded in the package metadata

`trusted` is the candidate keyword name; the final decision will be made by
RFC.

## 12. Stage 0 implementation

In the current prototype:

- ordinary heap values will use the Kofun runtime's tracing GC
- the type checker and runtime binding track `let own`
- the `take` statement and `take` parameters are implemented
- use-after-take is detected as E330
- owned values with a `close()` are automatically disposed at end of scope
- the narrow Stage 2 ownership slice reports E007 when a `Text` element is
  returned by value from an explicitly typed borrowed `List[Text]`
- borrow lifetimes, alias graphs, and async capture are not implemented

Stage 0 exists to validate the syntax and diagnostic UX. It is not a production
memory safety proof.
