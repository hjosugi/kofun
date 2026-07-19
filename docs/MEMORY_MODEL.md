# Memory model

## 1. Goals

Cofnのmemory modelは、次を同時に満たすことを目標にする。

- 普通のapplication codeではGC言語のように書ける
- file、socket、lock、transaction、GPU bufferはdeterministicに解放できる
- use-after-free、double free、data raceをsafe codeで防ぐ
- 日常コードにlifetime parameterを書かない
- compilerがunique valueを検出してin-place reuseできる
- embedded、real-time、high-performance向けにno-GC profileへ進める

## 2. Three memory domains

### 2.1 Copy values

`Int`、`Float`、`Bool`、small tupleなどはcopy valueである。

```cofn
let a = 42
let b = a
print(a)
print(b)
```

copyは明示的なheap allocationを必要としない。

### 2.2 Managed values

`Text`、普通の`List[T]`、records、closures、graph dataなどはmanaged heapに置ける。

```cofn
let names = ["A", "B", "C"]
let alias = names
```

通常のmanaged valueはGCによって回収する。言語表面ではimmutable-by-defaultなので、aliasがあってもdata raceやunexpected mutationを起こしにくい。

compilerは次を自由に最適化できる。

- stack allocation
- scalar replacement
- arena allocation
- reference counting
- tracing GC
- owned allocationへのpromotion
- in-place reuse

ただしobservable semanticsを変えてはならない。

### 2.3 Owned resources

external resourceまたはdeterministic cleanupが必要なvalueは`own`で束縛する。

```cofn
let own file = File.open("data.csv")
```

owned valueはaffineである。

- 0回または1回consumeできる
- 2回consumeできない
- scope終了時に未消費なら自動dropされる
- `take`後は元のbindingを使えない

linearではなくaffineにする理由は、早期returnや未使用resourceでもscope cleanupが安全に処理できるためである。

## 3. Parameter modes

### 3.1 `read T`

read-only、non-owning view。

```cofn
fn checksum(read bytes: Bytes) -> Int {
    # bytes cannot be mutated or consumed here
}
```

性質:

- 複数の`read` viewを同時に持てる
- 元valueをconsumeしない
- viewはv1では関数外へescapeできない
- compilerがlifetimeをlexical scopeから推論する

### 3.2 `edit T`

exclusive mutable view。

```cofn
fn normalize(edit values: Array[Float]) {
    # exclusive mutation is allowed
}
```

性質:

- 同じ期間に別の`read`または`edit` viewを作れない
- 元valueをconsumeしない
- v1ではnon-escaping
- mutationはeffectとして記録される

### 3.3 `take T`

ownership transfer。

```cofn
fn send(take socket: Socket, read payload: Bytes) {
    # socket is owned by this call
}
```

call site:

```cofn
let own socket = Socket.connect(address)
send(socket, payload)

# compile error: socket was taken
print(socket.peer())
```

`take`をcall siteにも書かせるかはUX testで決める。初期案ではparameter declarationだけに置き、compiler diagnosticでownership transferを明示する。高度なreviewが必要なAPIでは`send(take socket, payload)`というcall-site annotationを許可する案もbacklogに含める。

## 4. `let own`

```cofn
let own file = File.open(path)
```

このbindingには次の状態machineがある。

```text
uninitialized
    -> live
    -> taken
    -> dropped
```

禁止transition:

```text
taken -> read
taken -> edit
taken -> take
dropped -> any use
live + active edit -> another read/edit
live + active read -> edit/take
```

## 5. Branches

```cofn
let own socket = connect()

if should_send {
    send(socket)
} else {
    close(socket)
}
```

両branchでconsumeされるため、branch後は`sock`を使えない。

片方だけconsumeする場合も、conservativeなv1 checkerではbranch後に使えない。

```cofn
if should_send {
    send(socket)
}

# compile error in v1: socket may have been taken
```

将来はstate refinementで、boolean conditionとresource stateを関連付けられるようにする。

## 6. Loops

outer owned valueをloop内でconsumeする場合、loopが0回または褢数回実行される可能性がある。

```cofn
let own socket = connect()

while condition {
    send(socket) # rejected
}
```

安全な形:

```cofn
let mut pending: Socket? = connect()

while condition && pending != null {
    let own socket = pending.take()
    send(socket)
    pending = null
}
```

より良いstate APIはADTとpattern matchingの実装後に提供する。

## 7. Closures

closure captureは3種類に分類する。

```cofn
fn make_reader(read data: Bytes) -> fn() -> Int
fn make_editor(edit data: Buffer) -> fn() -> Void
fn make_owner(take data: Resource) -> fn() -> Void
```

v1 rule:

- `read` / `edit` captureはescaping closureに入れられない
- escaping closureはmanaged valueまたはtaken owned valueのみcaptureできる
- async taskへ渡す値は`Send`相当のauto traitを満たす
- thread間共有値は`Share`相当を満たす

## 8. GC design

production runtimeのdefaultはgenerational precise tracing GCを想定する。

### Nursery

- thread-local bump allocation
- small managed objects
- copying minor collection
- precise stack map

### Old generation

- compactingまたはregion-based collector
- large object space
- optional concurrent marking
- pinned object support

### Compiler cooperation

- safepoint insertion
- exact root map
- write barrier insertionとelimination
- escape analysis
- object layout metadata
- ownership-based allocation avoidance

### Operational controls

```text
COFN_GC_NURSERY_MB
COFN_GC_MAX_HEAP_MB
COFN_GC_PAUSE_TARGET_MS
COFN_GC_LOG
```

名称は未確定であり、production APIではmanifestとCLI configに統合する。

## 9. Owned-to-managed conversion

長く共有したいresource wrapperは明示的に`share`する。

```cofn
let own client = Client.connect(endpoint)
let shared = share(client)
```

`share`後:

- original owned bindingはtaken
- shared handleはGCまたはatomic reference countingで管理できる
- deterministic closeが必要なら`Shared[Client]`のprotocolに従う
- finalizerだけにcorrectnessを依存させない

## 10. Finalizers

GC finalizerはlast-resort cleanupであり、normal resource protocolでは使わない。

禁止する設計:

- transaction commitをfinalizerに任せる
- lock releaseのtimingをfinalizerに任せる
- file flushのcorrectnessをfinalizerに任せる

resourceはscope cleanup、`take`、`with`相当のresource scopeで処理する。

## 11. Unsafe boundary

safe language coreから外れる操作は、通常moduleと分離する。

予定例:

```cofn
import trusted.memory

trusted fn from_raw_pointer[T](ptr: Ptr[T], len: Int) -> Slice[T]
```

原則:

- `unsafe`を短いescape hatchとしてばらまかない
- trusted moduleがpreconditionとpostconditionを型・contractで公開する
- linterがtrusted surface areaを計測する
- package metadataにunsafe capabilityを記録する

keyword名は`trusted`を候補とし、最終決定はRFCで行う。

## 12. Stage 0 implementation

現在のprototypeでは:

- ordinary valuesはPython runtimeのGC上にある
- `let own`をtype checkerとruntime bindingが追跡する
- `take` statementと`take` parameterを実装している
- use-after-takeをE330で検出する
- scope終了時に`close()`を持つowned valueを自動disposeする
- borrow lifetime、alias graph、async captureは未実装

Stage 0はsyntaxとdiagnostic UXの検証用であり、production memory safety proofではない。
