"""Integration test: G722Decoder as a PCMSource for PCMFlow."""

import re


def test_external_source(dut):
    dut.expect("TEST start external_source", timeout=10)
    match = dut.expect(re.compile(rb"TEST done (\d+)/(\d+)"), timeout=30)
    passed, total = int(match.group(1)), int(match.group(2))
    assert passed == total, f"{total - passed} of {total} assertions failed"
    assert total > 0, "no assertions ran"
