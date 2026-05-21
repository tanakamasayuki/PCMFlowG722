"""Shared pytest hooks for PCMFlowOpus tests.

Mirrors the parent PCMFlow `tests/conftest.py` — wipes the per-test
`output/` directory before each test so host-profile artifacts don't
leak across runs. See the parent docs for the full rationale.
"""

import shutil
from pathlib import Path


def pytest_runtest_setup(item):
    output_dir = Path(item.fspath).parent / "output"
    if output_dir.exists():
        shutil.rmtree(output_dir)
