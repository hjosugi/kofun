"""The Kofun build system, built into the compiler.

There is no separate build tool. `kofun build` is the build system, the same way
`zig build` is: one binary, no manifest required for the simple case, and a
manifest only when a project outgrows a single file.

The structural advantage over Bazel, Ninja, and Make is that Kofun never spawns
a compiler process. Those tools model a build as a graph of *subprocesses*, so
every source file costs a fork, an exec, and a dynamic link before any
compilation happens -- typically 1-3 ms of pure overhead per file, which
dominates real builds. Kofun's code generator runs in-process in ~0.13 ms per
file, so that overhead is not reduced, it is absent.

Caching is content-addressed and uses early cutoff: an action key is the hash of
the source bytes, the compiler's own fingerprint, and the resolved target
configuration. Identical inputs restore a cached artifact instead of rebuilding.
Because code generation is deterministic, an unchanged rebuild is exact.
"""

from __future__ import annotations

import hashlib
import os
import shutil
import sys
import tomllib
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterable, Literal

from .c_backend import BackendFailure

CACHE_DIR = ".kofun"
MANIFEST_NAME = "kofun.toml"
TargetKind = Literal["binary", "library"]

_NAME_ALLOWED = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")


class BuildError(Exception):
    """A build could not be planned or executed."""


@dataclass(slots=True)
class Target:
    name: str
    kind: TargetKind
    srcs: list[Path]
    deps: list[str] = field(default_factory=list)

    @property
    def is_runnable(self) -> bool:
        return self.kind == "binary"


@dataclass(slots=True)
class Manifest:
    root: Path
    name: str
    targets: dict[str, Target]
    default_targets: list[str]

    @staticmethod
    def load(path: Path) -> "Manifest":
        try:
            raw = tomllib.loads(path.read_text(encoding="utf-8"))
        except tomllib.TOMLDecodeError as error:
            raise BuildError(f"{path}: invalid TOML: {error}") from error

        root = path.parent
        workspace = raw.get("workspace", {})
        if not isinstance(workspace, dict):
            raise BuildError(f"{path}: [workspace] must be a table")

        targets: dict[str, Target] = {}
        for name, body in (raw.get("target") or {}).items():
            targets[name] = _parse_target(path, root, name, body)

        if not targets:
            raise BuildError(f"{path}: no [target.*] entries")

        defaults = workspace.get("default_targets") or sorted(
            n for n, t in targets.items() if t.is_runnable
        )
        for name in defaults:
            if name not in targets:
                raise BuildError(f"{path}: default_targets names unknown target `{name}`")

        return Manifest(
            root=root,
            name=workspace.get("name", root.name),
            targets=targets,
            default_targets=list(defaults),
        )


def _parse_target(manifest: Path, root: Path, name: str, body: object) -> Target:
    if not isinstance(body, dict):
        raise BuildError(f"{manifest}: [target.{name}] must be a table")
    if not name or set(name) - _NAME_ALLOWED:
        raise BuildError(
            f"{manifest}: target name `{name}` must match [A-Za-z0-9_-]+"
        )

    unknown = set(body) - {"kind", "srcs", "deps"}
    if unknown:
        raise BuildError(
            f"{manifest}: [target.{name}] has unknown keys: {', '.join(sorted(unknown))}"
        )

    kind = body.get("kind", "binary")
    if kind not in ("binary", "library"):
        raise BuildError(
            f"{manifest}: [target.{name}] kind must be `binary` or `library`, got `{kind}`"
        )
    if kind == "library":
        # `library` is reserved rather than implemented. There is no module
        # system and no separate compilation yet, so a library target could not
        # be linked into anything -- it would only ever fail later, with a
        # worse message than this one.
        raise BuildError(
            f"{manifest}: [target.{name}] kind `library` is not supported yet; it "
            "needs the module system and separate compilation. Use `binary` "
            "targets for now."
        )

    raw_srcs = body.get("srcs")
    if not isinstance(raw_srcs, list) or not raw_srcs:
        raise BuildError(f"{manifest}: [target.{name}] needs a non-empty `srcs` list")

    srcs: list[Path] = []
    for entry in raw_srcs:
        if not isinstance(entry, str):
            raise BuildError(f"{manifest}: [target.{name}] srcs must be strings")
        candidate = Path(entry)
        if candidate.is_absolute() or ".." in candidate.parts:
            raise BuildError(
                f"{manifest}: [target.{name}] src `{entry}` must be relative and stay "
                "inside the workspace"
            )
        resolved = root / candidate
        if not resolved.is_file():
            raise BuildError(f"{manifest}: [target.{name}] src `{entry}` does not exist")
        srcs.append(resolved)

    deps = body.get("deps", [])
    if not isinstance(deps, list) or not all(isinstance(d, str) for d in deps):
        raise BuildError(f"{manifest}: [target.{name}] deps must be a list of strings")

    return Target(name=name, kind=kind, srcs=sorted(srcs), deps=list(deps))


def compiler_fingerprint() -> str:
    """Hash the compiler's own source, so changing it invalidates the cache.

    Without this, editing the code generator would leave stale artifacts in
    place and an incremental build would silently ship the old machine code.
    """
    digest = hashlib.sha256()
    package = Path(__file__).parent
    for name in sorted(p.name for p in package.glob("*.py")):
        digest.update(name.encode())
        digest.update((package / name).read_bytes())
    return digest.hexdigest()


def _resolve_order(manifest: Manifest, requested: Iterable[str]) -> list[Target]:
    """Return targets in dependency order, rejecting cycles and unknown names."""
    order: list[Target] = []
    state: dict[str, int] = {}   # 0 = visiting, 1 = done

    def visit(name: str, trail: tuple[str, ...]) -> None:
        if state.get(name) == 1:
            return
        if state.get(name) == 0:
            cycle = " -> ".join(trail[trail.index(name):] + (name,))
            raise BuildError(f"dependency cycle: {cycle}")
        target = manifest.targets.get(name)
        if target is None:
            origin = f" (required by {trail[-1]})" if trail else ""
            raise BuildError(f"unknown target `{name}`{origin}")
        state[name] = 0
        for dep in target.deps:
            visit(dep, trail + (name,))
        state[name] = 1
        order.append(target)

    for name in requested:
        visit(name, ())
    return order


@dataclass(slots=True)
class ActionResult:
    target: str
    output: Path
    cached: bool
    duration_ms: float
    error: str | None = None

    @property
    def ok(self) -> bool:
        return self.error is None


@dataclass(slots=True)
class BuildReport:
    results: list[ActionResult]
    total_ms: float

    @property
    def executed(self) -> int:
        return sum(1 for r in self.results if r.ok and not r.cached)

    @property
    def cached(self) -> int:
        return sum(1 for r in self.results if r.ok and r.cached)

    @property
    def failed(self) -> list[ActionResult]:
        return [r for r in self.results if not r.ok]

    @property
    def ok(self) -> bool:
        return not self.failed


class Cache:
    """Content-addressed artifact store under `.kofun/cache`."""

    def __init__(self, root: Path) -> None:
        self.directory = root / CACHE_DIR / "cache"

    def path_for(self, key: str) -> Path:
        # Shard on the first two hex digits to keep directories small.
        return self.directory / key[:2] / key[2:]

    def restore(self, key: str, destination: Path) -> bool:
        entry = self.path_for(key)
        if not entry.is_file():
            return False
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(entry, destination)
        destination.chmod(0o755)
        return True

    def store(self, key: str, source: Path) -> None:
        entry = self.path_for(key)
        entry.parent.mkdir(parents=True, exist_ok=True)
        # Write to a temporary name and rename, so a concurrent reader never
        # observes a half-written artifact.
        staging = entry.with_name(entry.name + f".{os.getpid()}.tmp")
        shutil.copy2(source, staging)
        staging.replace(entry)


class Builder:
    def __init__(
        self,
        manifest: Manifest,
        *,
        compile_target: Callable[[Target, Path], None],
        use_cache: bool = True,
        jobs: int = 0,
    ) -> None:
        self.manifest = manifest
        self.compile_target = compile_target
        self.use_cache = use_cache
        self.jobs = jobs or min(8, (os.cpu_count() or 2))
        self.cache = Cache(manifest.root)
        self._fingerprint = compiler_fingerprint()

    def output_path(self, target: Target) -> Path:
        suffix = "bin" if target.kind == "binary" else "lib"
        return self.manifest.root / CACHE_DIR / suffix / target.name

    def action_key(self, target: Target) -> str:
        """Hash everything that can change the artifact."""
        digest = hashlib.sha256()
        digest.update(b"kofun-action-v1\0")
        digest.update(self._fingerprint.encode())
        digest.update(target.name.encode())
        digest.update(target.kind.encode())
        for src in target.srcs:
            digest.update(str(src.relative_to(self.manifest.root)).encode())
            digest.update(src.read_bytes())
        for dep in sorted(target.deps):
            # Depend on the dependency's key, so a change propagates.
            digest.update(self.action_key(self.manifest.targets[dep]).encode())
        return digest.hexdigest()

    def build(
        self,
        requested: Iterable[str] | None = None,
        *,
        on_event: Callable[[str, Target, ActionResult | None], None] | None = None,
    ) -> BuildReport:
        import time

        names = list(requested or self.manifest.default_targets)
        order = _resolve_order(self.manifest, names)
        started = time.perf_counter()

        results: dict[str, ActionResult] = {}
        remaining = {t.name: set(t.deps) for t in order}
        by_name = {t.name: t for t in order}
        pending = list(order)

        def notify(kind: str, target: Target, result: ActionResult | None = None) -> None:
            if on_event is not None:
                on_event(kind, target, result)

        with ThreadPoolExecutor(max_workers=self.jobs) as pool:
            while pending:
                # A wave is every target whose dependencies are already built.
                wave = [
                    t for t in pending
                    if not (remaining[t.name] - set(results))
                ]
                if not wave:
                    raise BuildError("internal error: build made no progress")

                # Skip anything whose dependency failed, rather than building
                # against a stale or missing artifact.
                runnable = []
                for target in wave:
                    broken = [d for d in target.deps if not results[d].ok]
                    if broken:
                        results[target.name] = ActionResult(
                            target=target.name,
                            output=self.output_path(target),
                            cached=False,
                            duration_ms=0.0,
                            error=f"skipped: dependency `{broken[0]}` failed",
                        )
                        notify("skip", target, results[target.name])
                    else:
                        runnable.append(target)

                for target in runnable:
                    notify("start", target)
                for target, result in zip(
                    runnable, pool.map(self._run_one, runnable)
                ):
                    results[target.name] = result
                    notify("finish", target, result)

                pending = [t for t in pending if t.name not in results]

        return BuildReport(
            results=[results[t.name] for t in order],
            total_ms=(time.perf_counter() - started) * 1000.0,
        )

    def _run_one(self, target: Target) -> ActionResult:
        import time

        started = time.perf_counter()
        output = self.output_path(target)
        key = self.action_key(target)

        if self.use_cache and self.cache.restore(key, output):
            return ActionResult(
                target=target.name,
                output=output,
                cached=True,
                duration_ms=(time.perf_counter() - started) * 1000.0,
            )

        try:
            output.parent.mkdir(parents=True, exist_ok=True)
            self.compile_target(target, output)
        except (BackendFailure, BuildError, OSError) as error:
            return ActionResult(
                target=target.name,
                output=output,
                cached=False,
                duration_ms=(time.perf_counter() - started) * 1000.0,
                error=str(error),
            )

        if self.use_cache and output.is_file():
            self.cache.store(key, output)

        return ActionResult(
            target=target.name,
            output=output,
            cached=False,
            duration_ms=(time.perf_counter() - started) * 1000.0,
        )


def find_manifest(start: Path) -> Path | None:
    """Walk upwards looking for a manifest, like git looks for .git."""
    current = start.resolve()
    for candidate in (current, *current.parents):
        manifest = candidate / MANIFEST_NAME
        if manifest.is_file():
            return manifest
    return None


def clean(root: Path, *, cache: bool = False) -> list[Path]:
    """Remove build outputs; with `cache`, remove the artifact store too."""
    removed: list[Path] = []
    base = root / CACHE_DIR
    targets = [base / "bin", base / "lib"]
    if cache:
        targets.append(base / "cache")
    for directory in targets:
        if directory.exists():
            shutil.rmtree(directory)
            removed.append(directory)
    return removed
