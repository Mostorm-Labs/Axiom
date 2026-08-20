from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SCRIPT = Path(__file__).with_name("behavior_conformance.py")
SPEC = importlib.util.spec_from_file_location("behavior_conformance", SCRIPT)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class BehaviorConformanceTest(unittest.TestCase):
    def record(self, platform: str) -> dict:
        return {
            "platform": platform,
            "digest": "a" * 32,
            "behavior": {key: True for key in MODULE.REQUIRED_BEHAVIOR},
            "layout": {
                "height": 20.0,
                "lines": [{"start": [0, 0], "end": [0, 1]}],
                "clusters": [{"start": [0, 0], "end": [0, 1]}],
                "selection": [[0, 0, 1, 1]],
                "diagnostics": [],
            },
            "lifecycle": {"cycles": 100, "failures": 0},
            "performance": {
                "input_caret_samples": 120,
                "input_caret_warmup_samples": 20,
                "input_caret_p50_ms": 1.0,
                "input_caret_p95_ms": 1.0,
                "input_caret_p99_ms": 1.0,
                "input_caret_max_ms": 1.0,
                "full_layout_samples": 30,
                "full_layout_warmup_samples": 5,
                "full_layout_p50_ms": 2.0,
                "full_layout_p95_ms": 2.0,
                "full_layout_p99_ms": 2.0,
                "full_layout_max_ms": 2.0,
            },
        }

    def run_records(self, records: list[dict]) -> subprocess.CompletedProcess[str]:
        with tempfile.TemporaryDirectory() as temporary:
            paths = []
            for index, record in enumerate(records):
                path = Path(temporary) / f"{index}.json"
                path.write_text(json.dumps(record), encoding="utf-8")
                paths.append(str(path))
            return subprocess.run(
                [sys.executable, str(SCRIPT), *paths], text=True,
                capture_output=True,
            )

    def test_accepts_complete_identical_matrix(self) -> None:
        result = self.run_records([
            self.record("web"), self.record("windows"), self.record("android"),
        ])
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_accepts_equivalent_json_number_spelling(self) -> None:
        records = [self.record("web"), self.record("windows"), self.record("android")]
        records[0]["layout"]["height"] = 20
        records[0]["layout"]["selection"][0][0] = 0
        result = self.run_records(records)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_real_layout_difference_without_tolerance(self) -> None:
        records = [self.record("web"), self.record("windows"), self.record("android")]
        records[2]["layout"]["height"] = 20.000001
        result = self.run_records(records)
        self.assertNotEqual(result.returncode, 0)

    def test_rejects_false_behavior_or_incomplete_geometry(self) -> None:
        records = [self.record("web"), self.record("windows"), self.record("android")]
        records[1]["behavior"]["undo"] = False
        result = self.run_records(records)
        self.assertNotEqual(result.returncode, 0)

        records = [self.record("web"), self.record("windows"), self.record("android")]
        for record in records:
            record["layout"]["clusters"] = []
        result = self.run_records(records)
        self.assertNotEqual(result.returncode, 0)

    def test_rejects_incomplete_performance_distribution(self) -> None:
        records = [self.record("web"), self.record("windows"), self.record("android")]
        del records[0]["performance"]["input_caret_p99_ms"]
        result = self.run_records(records)
        self.assertNotEqual(result.returncode, 0)

        records = [self.record("web"), self.record("windows"), self.record("android")]
        for record in records:
            record["layout"]["diagnostics"] = ["unresolved-glyphs"]
        result = self.run_records(records)
        self.assertNotEqual(result.returncode, 0)

    def test_android_emulator_timing_is_observational(self) -> None:
        records = [self.record("web"), self.record("windows"), self.record("android")]
        records[2]["performance"].update({
            "input_caret_p50_ms": 20.0,
            "input_caret_p95_ms": 40.0,
            "input_caret_p99_ms": 50.0,
            "input_caret_max_ms": 60.0,
            "full_layout_p50_ms": 40.0,
            "full_layout_p95_ms": 80.0,
            "full_layout_p99_ms": 90.0,
            "full_layout_max_ms": 100.0,
        })
        result = self.run_records(records)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rejects_non_android_latency_regression(self) -> None:
        records = [self.record("web"), self.record("windows"), self.record("android")]
        records[0]["performance"].update({
            "full_layout_p50_ms": 40.0,
            "full_layout_p95_ms": 80.0,
            "full_layout_p99_ms": 90.0,
            "full_layout_max_ms": 100.0,
        })
        result = self.run_records(records)
        self.assertNotEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
