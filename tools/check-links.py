#!/usr/bin/env python3
"""Verifica links relativos em Markdown (quebrados falham o lint)."""

from __future__ import annotations

import os
import re
import sys

FILES = [
    "README.md",
    "CONTRIBUTING.md",
    "CHANGELOG.md",
    "docs/BUILD.md",
    "docs/DEVICES.md",
    "docs/BOOTSPLASH.md",
    "docs/HANDOFF.md",
    "docs/QA-CHECKLIST.md",
    "buildroot/board/giochi/dts/README",
]

LINK = re.compile(r"\]\(([^)\s]+)\)")


def main() -> int:
    broken = 0
    checked = 0
    for rel in FILES:
        if not os.path.exists(rel):
            continue
        checked += 1
        base = os.path.dirname(rel) or "."
        text = open(rel, encoding="utf-8").read()
        for link in LINK.findall(text):
            if link.startswith(("http://", "https://", "mailto:", "#")):
                continue
            path = link.split("#", 1)[0]
            if not path:
                continue
            full = os.path.normpath(os.path.join(base, path))
            if not os.path.exists(full):
                print(f"link quebrado: {rel} -> {link}", file=sys.stderr)
                broken += 1
    if broken:
        print(f"check-links: {broken} link(s) quebrado(s)", file=sys.stderr)
        return 1
    print(f"ok ({checked} arquivo(s))")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
