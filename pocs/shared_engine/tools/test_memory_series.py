from __future__ import annotations

import unittest

from memory_series import analyze


def series(values: list[int]) -> list[dict[str, int]]:
    return [
        {"elapsed_ms": index * 5_000, "bytes": value}
        for index, value in enumerate(values)
    ]


class MemorySeriesTest(unittest.TestCase):
    def test_stable_series_passes(self) -> None:
        report = analyze(series([100, 101, 100, 102, 101, 100, 102, 101, 100, 101, 100]))
        self.assertTrue(report["passed"])
        self.assertFalse(report["sustained_growth_observed"])

    def test_material_sustained_growth_fails(self) -> None:
        mib = 1024 * 1024
        report = analyze(series([100 * mib] * 4 + [110 * mib, 120 * mib, 130 * mib] + [140 * mib] * 4))
        self.assertFalse(report["passed"])
        self.assertTrue(report["sustained_growth_observed"])

    def test_small_allocator_noise_passes(self) -> None:
        mib = 1024 * 1024
        report = analyze(series([100 * mib] * 4 + [101 * mib] * 3 + [104 * mib] * 4))
        self.assertTrue(report["passed"])

    def test_sparse_series_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            analyze(series([100] * 9))


if __name__ == "__main__":
    unittest.main()
