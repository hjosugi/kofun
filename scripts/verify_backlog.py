#!/usr/bin/env python3
from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BACKLOG = ROOT / "backlog"
ROW = re.compile(r"^\| (FROST-(\d{5})) \| (planned|prototype|done) \| (P[0-3]) \| ([^|]+) \| ([^|]+) \|")


def main() -> int:
    files = sorted(BACKLOG.glob("issues-*.md"))
    if len(files) != 27:
        raise SystemExit(f"expected 27 area files, found {len(files)}")
    ids: list[int] = []
    textual: set[str] = set()
    fingerprints: set[str] = set()
    per_file: dict[str, int] = {}
    for path in files:
        count = 0
        for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
            if not line.startswith("| FROST-"):
                continue
            match = ROW.match(line)
            if not match:
                raise SystemExit(f"invalid issue row: {path}:{line_number}")
            issue_id = match.group(1)
            numeric = int(match.group(2))
            if issue_id in textual:
                raise SystemExit(f"duplicate ID: {issue_id}")
            textual.add(issue_id)
            ids.append(numeric)
            fingerprint_match = re.search(r"`([0-9a-f]{12})` \|$", line)
            if not fingerprint_match:
                raise SystemExit(f"missing fingerprint: {path}:{line_number}")
            fingerprint = fingerprint_match.group(1)
            if fingerprint in fingerprints:
                raise SystemExit(f"duplicate fingerprint: {fingerprint}")
            fingerprints.add(fingerprint)
            cells = [cell.strip() for cell in line.strip("|").split("|")]
            if len(cells) != 10 or any(not cell for cell in cells):
                raise SystemExit(f"empty or malformed cell: {path}:{line_number}")
            count += 1
        if count != 500:
            raise SystemExit(f"{path.name}: expected 500 issues, found {count}")
        per_file[path.name] = count
    expected = list(range(1, 13_501))
    if ids != expected:
        missing = sorted(set(expected) - set(ids))[:20]
        extra = sorted(set(ids) - set(expected))[:20]
        raise SystemExit(f"ID sequence mismatch; missing={missing}, extra={extra}")
    summary = json.loads((BACKLOG / "summary.json").read_text(encoding="utf-8"))
    if summary.get("total") != 13_500:
        raise SystemExit(f"summary total is {summary.get('total')}, expected 13500")
    print(f"verified {len(ids):,} unique issues across {len(files)} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
