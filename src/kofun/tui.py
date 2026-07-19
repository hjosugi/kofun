"""Terminal UI for `kofun build`.

Two renderers behind one interface. `LiveRenderer` draws an in-place progress
view on a TTY; `PlainRenderer` emits append-only lines everywhere else -- pipes,
CI, dumb terminals, and `--no-tui`. Choosing the wrong one makes CI logs
unreadable, so the detection is deliberate rather than incidental.

No third-party dependency. The escape sequences used here are the small subset
every terminal since the 1980s implements.
"""

from __future__ import annotations

import os
import shutil
import sys
import threading
import time
from dataclasses import dataclass, field
from typing import Protocol, TextIO

from .build_system import ActionResult, BuildReport, Target

ESC = "\x1b["
HIDE_CURSOR = f"{ESC}?25l"
SHOW_CURSOR = f"{ESC}?25h"
CLEAR_LINE = f"{ESC}2K"

DIM = f"{ESC}2m"
BOLD = f"{ESC}1m"
RED = f"{ESC}31m"
GREEN = f"{ESC}32m"
YELLOW = f"{ESC}33m"
BLUE = f"{ESC}34m"
CYAN = f"{ESC}36m"
RESET = f"{ESC}0m"

SPINNER = "⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏"


def supports_tui(stream: TextIO, *, force_off: bool = False) -> bool:
    """Decide whether a live view is appropriate for this stream.

    CI systems set `CI`, and many set `TERM=dumb`. Both make in-place rendering
    actively harmful, because the escape sequences end up in the archived log.
    `NO_COLOR` is honoured as the widely-supported opt-out convention.
    """
    if force_off:
        return False
    if os.environ.get("CI") or os.environ.get("NO_COLOR"):
        return False
    if os.environ.get("TERM", "") in ("", "dumb"):
        return False
    return bool(getattr(stream, "isatty", lambda: False)())


class Renderer(Protocol):
    def start(self, total: int) -> None: ...
    def on_event(self, kind: str, target: Target, result: ActionResult | None) -> None: ...
    def finish(self, report: BuildReport) -> None: ...


@dataclass
class PlainRenderer:
    """Append-only output. Safe for pipes, CI, and logs."""

    stream: TextIO = field(default_factory=lambda: sys.stderr)
    total: int = 0
    done: int = 0

    def start(self, total: int) -> None:
        self.total = total
        self.done = 0

    def on_event(self, kind: str, target: Target, result: ActionResult | None) -> None:
        if kind == "start" or result is None:
            return
        self.done += 1
        if not result.ok:
            status = "FAILED"
        elif result.cached:
            status = "cached"
        else:
            status = f"{result.duration_ms:.1f} ms"
        print(
            f"[{self.done}/{self.total}] {target.name} ({status})",
            file=self.stream,
        )
        if not result.ok:
            print(f"    {result.error}", file=self.stream)

    def finish(self, report: BuildReport) -> None:
        print(summary_line(report, color=False), file=self.stream)


@dataclass
class LiveRenderer:
    """In-place progress view for interactive terminals."""

    stream: TextIO = field(default_factory=lambda: sys.stderr)
    total: int = 0
    done: int = 0
    running: dict[str, float] = field(default_factory=dict)
    completed: list[tuple[str, ActionResult]] = field(default_factory=list)
    _lines_drawn: int = 0
    _lock: threading.Lock = field(default_factory=threading.Lock)
    _stop: threading.Event = field(default_factory=threading.Event)
    _thread: threading.Thread | None = None
    _frame: int = 0

    def start(self, total: int) -> None:
        self.total = total
        self.done = 0
        self.stream.write(HIDE_CURSOR)
        self.stream.flush()
        self._thread = threading.Thread(target=self._animate, daemon=True)
        self._thread.start()

    def _animate(self) -> None:
        while not self._stop.wait(0.08):
            with self._lock:
                self._frame += 1
                self._draw()

    def on_event(self, kind: str, target: Target, result: ActionResult | None) -> None:
        with self._lock:
            if kind == "start":
                self.running[target.name] = time.perf_counter()
            else:
                self.running.pop(target.name, None)
                if result is not None:
                    self.done += 1
                    self.completed.append((target.name, result))
            self._draw()

    def _draw(self) -> None:
        self._erase()
        lines: list[str] = []

        # Only the most recent finished targets, so the view stays a fixed size.
        for name, result in self.completed[-6:]:
            if not result.ok:
                mark, colour, detail = "✗", RED, result.error or "failed"
            elif result.cached:
                mark, colour, detail = "•", DIM, "cached"
            else:
                mark, colour, detail = "✓", GREEN, f"{result.duration_ms:.1f} ms"
            lines.append(f"  {colour}{mark}{RESET} {name} {DIM}{detail}{RESET}")

        spin = SPINNER[self._frame % len(SPINNER)]
        for name, started in list(self.running.items())[:4]:
            elapsed = (time.perf_counter() - started) * 1000.0
            lines.append(f"  {CYAN}{spin}{RESET} {name} {DIM}{elapsed:.0f} ms{RESET}")

        lines.append(self._progress_bar())

        self.stream.write("\n".join(lines) + "\n")
        self.stream.flush()
        self._lines_drawn = len(lines)

    def _progress_bar(self) -> str:
        width = max(20, min(shutil.get_terminal_size((80, 24)).columns - 30, 40))
        filled = 0 if not self.total else int(width * self.done / self.total)
        bar = f"{BLUE}{'━' * filled}{RESET}{DIM}{'━' * (width - filled)}{RESET}"
        return f"  {bar} {BOLD}{self.done}/{self.total}{RESET}"

    def _erase(self) -> None:
        if self._lines_drawn:
            self.stream.write(f"{ESC}{self._lines_drawn}A")
            self.stream.write((CLEAR_LINE + "\n") * self._lines_drawn)
            self.stream.write(f"{ESC}{self._lines_drawn}A")
            self._lines_drawn = 0

    def finish(self, report: BuildReport) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=0.5)
        with self._lock:
            self._erase()
            self.stream.write(SHOW_CURSOR)
            for result in report.failed:
                print(
                    f"  {RED}✗{RESET} {BOLD}{result.target}{RESET}\n    {result.error}",
                    file=self.stream,
                )
            print(summary_line(report, color=True), file=self.stream)
            self.stream.flush()


def summary_line(report: BuildReport, *, color: bool) -> str:
    parts = [f"{report.executed} executed", f"{report.cached} cached"]
    if report.failed:
        parts.append(f"{len(report.failed)} failed")
    body = ", ".join(parts) + f" in {report.total_ms:.0f} ms"
    if not color:
        return f"kofun: {body}"
    tint = RED if report.failed else GREEN
    return f"{BOLD}kofun{RESET}: {tint}{body}{RESET}"


def make_renderer(*, no_tui: bool = False, stream: TextIO | None = None) -> Renderer:
    target = stream if stream is not None else sys.stderr
    if supports_tui(target, force_off=no_tui):
        return LiveRenderer(stream=target)
    return PlainRenderer(stream=target)
