"""Smoke test — verifies the build and harness wiring.

If this builds, the bridge code and the vendored libg722 codec sources
have been picked up correctly by the Arduino library loader.
"""


def test_smoke(dut):
    dut.expect("SMOKE ready", timeout=10)
