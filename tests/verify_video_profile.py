"""Static consistency checks for the video-style Geode fork.

Run with: python verify_video_profile.py
These tests inspect project configuration only. They do not emulate Geometry Dash.
"""
from __future__ import annotations

import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
MOD = json.loads((ROOT / "mod.json").read_text(encoding="utf-8"))


class VideoProfileTests(unittest.TestCase):
    def test_geode_metadata_and_settings(self):
        self.assertEqual(MOD["id"], "geodepredictive.inputbuffer")
        self.assertEqual(MOD["version"], "v4.6.0")
        self.assertEqual(MOD["settings"]["stage"]["default"], "Stage 1")
        self.assertEqual(
            MOD["settings"]["stage"]["one-of"],
            ["Off", "Stage 1", "Stage 2", "Stage 3", "Stage 4"],
        )
        self.assertTrue(MOD["settings"]["auto-scan"]["default"])
        self.assertFalse(MOD["settings"]["route-snap"]["default"])

    def test_expected_stage_windows_are_encoded(self):
        expected = {
            "Stage 1": (2, 2),
            "Stage 2": (4, 4),
            "Stage 3": (8, 8),
            "Stage 4": (16, 8),
        }
        lines = [line for line in SOURCE.splitlines() if '"Stage ' in line and 'true, false' in line]
        self.assertEqual(len(lines), 4, f"expected four enabled stages, found {len(lines)}")
        for name, (all_ticks, wave_ticks) in expected.items():
            line = next((line for line in lines if f'"{name}"' in line), None)
            self.assertIsNotNone(line, f"missing {name} stage")
            values = [float(v[:-1]) for v in re.findall(r"\d+\.\d+f", line)]
            self.assertEqual(len(values), 8, f"{name} should configure eight input windows")
            self.assertEqual(round(values[0] / 0.25), all_ticks, f"{name} all-tick limit")
            self.assertEqual(round(values[6] / 0.25), wave_ticks, f"{name} wave-tick limit")

    def test_console_matches_video_profile(self):
        for required in (
            "Geode Predictive Input Buffer - Live Console",
            "[InputBuffer] v4.6 LOADED (Wave Spikes/Saws Trajectory Fix)",
            "[InputBuffer] Starting Mode: STAGE 1 (2 All / 2 Wave)",
            "2/2 -> 4/4 -> 8/8 -> 16/8 -> OFF",
            "SAVED +{} TICKS",
            "Frame: @ -> Delayed (@)",
            "[Limit: {}]",
        ):
            self.assertIn(required, SOURCE, f"console text missing: {required}")
        self.assertIn("{:.1f}f / {:.1f}f @ 60fps", SOURCE)

    def test_no_input_buffer_visuals_enabled(self):
        for required in (
            "c.hud        = false;",
            "c.hudVerbose = false;",
            "c.overlay    = false;",
            "c.orbHelper  = false;",
            "c.scanView   = false;",
            "c.showBadge  = false;",
            "c.accuracy   = false;",
        ):
            self.assertIn(required, SOURCE)
        self.assertIn("Deliberately do not create the Input Buffer window", SOURCE)
        self.assertIn("no Input Buffer button", SOURCE)

    def test_automatic_paused_scan_is_bounded_and_console_only(self):
        self.assertIn("constexpr int    SCAN_SLICE_US           = 2000", SOURCE)
        self.assertIn('getSettingValue<bool>("auto-scan")', SOURCE)
        self.assertIn("s_sim.autoScanOnPause(this)", SOURCE)
        self.assertIn("Auto scan progress: {}%", SOURCE)
        self.assertIn("Auto scan finished: {}", SOURCE)
        self.assertIn("m_autoScanComplete = pctAt(pl, m_scanX) >= 99.5f", SOURCE)
        self.assertIn("routeMerge()", SOURCE)
        # A partial scan can restart from a later pause; no m_scanState == Done guard.
        auto_scan = SOURCE[SOURCE.index("void autoScanOnPause("):SOURCE.index("// The best version's inputs")]
        self.assertNotIn("m_scanState == ScanState::Done", auto_scan)
        # The scan finisher must not create an in-game notification in this profile.
        finish = SOURCE[SOURCE.index("void finishScan("):SOURCE.index("// Called once per frame by the scan runner.")]
        self.assertNotIn("Notification::create", finish)

    def test_f6_cycles_five_modes_then_wraps(self):
        self.assertIn("int const next = (cur == STAGE_COUNT - 1) ? 0 : cur + 1;", SOURCE)
        self.assertEqual(MOD["settings"]["stage"]["one-of"], ["Off", "Stage 1", "Stage 2", "Stage 3", "Stage 4"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
