#!/usr/bin/env python3
"""Refresh the vendored sippy/libg722 snapshot under src/external/libg722/.

Maintainer tool. Not invoked from CI (PCMFlowG722 adopts upstream-
tracking level L0; see SPEC.md §9). Run by hand when you want to pull
in a newer upstream snapshot:

    python tools/sync_libg722.py --apply

Without --apply the script just reports what would change. It clones
the upstream repo into a temporary directory, copies the codec-only
file subset (the Python bindings, CMake build, and test harness are
intentionally not vendored), and updates src/external/UPSTREAM.lock
with the new commit SHA and the date of the sync.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import pathlib
import shutil
import subprocess
import sys
import tempfile

UPSTREAM_URL = "https://github.com/sippy/libg722.git"

# The codec is self-contained in this small set of files. Everything
# else upstream (test harness, Python bindings, CMake/Make build) is
# intentionally NOT vendored — see src/external/LICENSE_libg722.md.
VENDORED_FILES = [
    "g722.h",
    "g722_codec.h",
    "g722_common.h",
    "g722_private.h",
    "g722_encoder.h",
    "g722_decoder.h",
    "g722_encode.c",
    "g722_decode.c",
]

VENDOR_DIR = pathlib.Path("src/external/libg722")
LOCK_FILE = pathlib.Path("src/external/UPSTREAM.lock")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--apply",
        action="store_true",
        help="Apply changes. Without this flag, only report the diff.",
    )
    parser.add_argument(
        "--ref",
        default="HEAD",
        help="Upstream git ref to sync to (default: HEAD on the default branch).",
    )
    return parser.parse_args()


def run(cmd: list[str], cwd: pathlib.Path | None = None) -> str:
    result = subprocess.run(
        cmd, cwd=cwd, check=True, capture_output=True, text=True
    )
    return result.stdout.strip()


def main() -> int:
    args = parse_args()

    if not VENDOR_DIR.parent.exists():
        print(f"error: {VENDOR_DIR.parent} not found — run from repo root.", file=sys.stderr)
        return 2

    with tempfile.TemporaryDirectory() as tmp_str:
        tmp = pathlib.Path(tmp_str)
        clone_dir = tmp / "libg722"
        print(f"Cloning {UPSTREAM_URL} ...")
        run(["git", "clone", "--depth=50", UPSTREAM_URL, str(clone_dir)])
        if args.ref != "HEAD":
            run(["git", "checkout", args.ref], cwd=clone_dir)
        sha = run(["git", "rev-parse", "HEAD"], cwd=clone_dir)
        date = _dt.datetime.now(_dt.timezone.utc).strftime("%Y-%m-%d")

        print(f"Upstream commit: {sha}")

        # Compare and (optionally) copy.
        changed: list[str] = []
        VENDOR_DIR.mkdir(parents=True, exist_ok=True)
        for name in VENDORED_FILES:
            src = clone_dir / name
            dst = VENDOR_DIR / name
            if not src.exists():
                print(f"warning: upstream missing {name}", file=sys.stderr)
                continue
            new_bytes = src.read_bytes()
            old_bytes = dst.read_bytes() if dst.exists() else b""
            if new_bytes != old_bytes:
                changed.append(name)
                if args.apply:
                    shutil.copy2(src, dst)

        if not changed:
            print("Vendored snapshot is already up to date.")
            return 0

        print(f"{'Updated' if args.apply else 'Would update'} {len(changed)} file(s):")
        for name in changed:
            print(f"  - {name}")

        if args.apply:
            update_lock(sha, date)
            print(f"Updated {LOCK_FILE} (sha={sha}, checked={date}).")
            print("Don't forget to bump the CHANGELOG entry under ## Unreleased.")
        else:
            print(f"\nRun with --apply to write changes and refresh {LOCK_FILE}.")
        return 0


def update_lock(sha: str, date: str) -> None:
    import re

    if not LOCK_FILE.exists():
        LOCK_FILE.write_text(
            f'upstream = "{UPSTREAM_URL.removesuffix(".git")}"\n'
            f'commit   = "{sha}"\n'
            f'checked  = "{date}"\n',
            encoding="utf-8",
        )
        return

    content = LOCK_FILE.read_text(encoding="utf-8")
    content = re.sub(r'^commit\s*=\s*".*"$', f'commit   = "{sha}"', content, flags=re.MULTILINE)
    content = re.sub(r'^checked\s*=\s*".*"$', f'checked  = "{date}"', content, flags=re.MULTILINE)
    LOCK_FILE.write_text(content, encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
