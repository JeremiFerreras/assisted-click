"""Static integration checks for the video-style Geode fork.

Run with: python tests/verify_video_profile.py
These checks inspect source/configuration only; they do not emulate GD.
"""
from __future__ import annotations

import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "src" / "main.cpp").read_text(encoding="utf-8")
ORB_CORE = (ROOT / "src" / "OrbAssistCore.hpp").read_text(encoding="utf-8")
MOD = json.loads((ROOT / "mod.json").read_text(encoding="utf-8"))


class VideoProfileTests(unittest.TestCase):
    def test_geode_metadata_and_settings(self):
        self.assertEqual(MOD["id"], "geodepredictive.inputbuffer")
        self.assertEqual(MOD["version"], "v4.7.0")
        self.assertEqual(MOD["settings"]["stage"]["default"], "Stage 1")
        self.assertEqual(
            MOD["settings"]["stage"]["one-of"],
            ["Off", "Stage 1", "Stage 2", "Stage 3", "Stage 4"],
        )
        self.assertTrue(MOD["settings"]["auto-scan"]["default"])
        self.assertFalse(MOD["settings"]["route-snap"]["default"])
        self.assertTrue(MOD["settings"]["orb-helper"]["default"])
        self.assertIn("no prior orb calibration", MOD["settings"]["orb-helper"]["description"])

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
            "[InputBuffer] v4.7 loaded (Orb Assist / Level Scan)",
            "[InputBuffer] Starting Mode: STAGE 1 (2 All / 2 Wave)",
            "2/2 -> 4/4 -> 8/8 -> 16/8 -> OFF",
            "[OrbAssist] {}",
        ):
            self.assertIn(required, SOURCE, f"console text missing: {required}")
        self.assertIn("{:.1f}f / {:.1f}f @ 60fps", SOURCE)
        loaded = SOURCE[SOURCE.index("$on_mod(Loaded)"):SOURCE.index("class $modify(BufKeyboard") if "class $modify(BufKeyboard" in SOURCE else SOURCE.index("$on_mod(Loaded)")+2000]
        self.assertNotIn("LogConsole::open()", loaded)
        self.assertNotIn("AllocConsole()", loaded)

    def test_no_input_buffer_visuals_enabled(self):
        for required in (
            "c.hud        = false;",
            "c.hudVerbose = false;",
            "c.overlay    = false;",
            "c.scanView   = false;",
            "c.showBadge  = false;",
            "c.accuracy   = false;",
        ):
            self.assertIn(required, SOURCE)
        self.assertIn("Deliberately do not create the Input Buffer window", SOURCE)
        self.assertIn("no Input Buffer button", SOURCE)
        self.assertIn('c.orbHelper  = mod->getSettingValue<bool>("orb-helper");', SOURCE)

    def test_automatic_paused_scan_is_bounded_and_console_only(self):
        self.assertIn("constexpr int    SCAN_SLICE_US           = 2000", SOURCE)
        self.assertIn('getSettingValue<bool>("auto-scan")', SOURCE)
        self.assertIn("s_sim.autoScanOnPause(this)", SOURCE)
        self.assertNotIn("Auto scan progress: {}%", SOURCE)
        self.assertNotIn("Auto scan finished: {}", SOURCE)
        self.assertIn("m_autoScanComplete = pctAt(pl, m_scanX) >= 99.5f", SOURCE)
        self.assertIn("routeMerge()", SOURCE)
        auto_scan = SOURCE[SOURCE.index("void autoScanOnPause("):SOURCE.index("// The best version's inputs")]
        self.assertNotIn("m_scanState == ScanState::Done", auto_scan)
        finish = SOURCE[SOURCE.index("void finishScan("):SOURCE.index("// Called once per frame by the scan runner.")]
        self.assertNotIn("Notification::create", finish)

    def test_orb_assist_is_deterministic_and_independent(self):
        self.assertTrue((ROOT / "src" / "OrbAssistCore.hpp").exists())
        self.assertIn("orbassist::modelPhysics", SOURCE)
        self.assertIn("orbassist::chooseSafeTick", SOURCE)
        self.assertNotIn("orbModelAvailable(", SOURCE)
        self.assertIn("if (m_orbAssistSimulation) sim->m_yStart = real->m_yStart;", SOURCE)
        self.assertIn("sim->m_isAccelerating = real->m_isAccelerating;", SOURCE)
        self.assertIn("sampleBaseline = sampleEvery;", SOURCE)
        self.assertIn("static_cast<double>(getValidDt()) * 0.90", SOURCE)
        self.assertIn("sim->m_ringJumpRelated = real->m_ringJumpRelated;", SOURCE)
        self.assertIn("sim->m_padRingRelated = real->m_padRingRelated;", SOURCE)
        self.assertIn("p->m_stateRingJump = false; // m_isJumpUnused", SOURCE)
        self.assertIn("p->m_hasEverHitRing = true;", SOURCE)
        self.assertIn("m_simMovers = m_orbAssistSimulation", SOURCE)
        self.assertIn("if (m_orbAssistSimulation && isModelledOrb(type)) return;", SOURCE)
        self.assertIn("(!m_orbAssistSimulation || m_orbAssistDiscoveryOnly)", SOURCE)
        self.assertIn("m_orbAssistDiscoveryOnly = true;", SOURCE)
        self.assertIn("discovery.push = false;", SOURCE)
        self.assertIn("discovery.horizon = std::clamp(std::max(ox.horizon, scanTicks + 1)", SOURCE)
        self.assertIn("runSim(discovery, 0, FollowUp{}, scanTicks + 1)", SOURCE)
        self.assertNotIn("runSim(ox, scanTicks + 1, FollowUp{}, 1)", SOURCE)
        self.assertIn("i > m_simFirstRingLast", SOURCE)
        self.assertIn("touchingOrbObj(pl->m_player1, m_orbHelpRing)", SOURCE)
        self.assertIn("candidate.orbAssistComplete && candidate.orbAssistModelValid", SOURCE)
        self.assertIn("candidate.orbUsed && candidate.exactOrbContact", SOURCE)
        self.assertIn("candidate.usedRing == target", SOURCE)
        self.assertIn("orbMotionKnown(idle.firstRing)", SOURCE)
        self.assertIn("ORB_HELP_BASE           = 48", SOURCE)
        self.assertIn("ORB_HELP_OVERLAP_TICKS  = 64", SOURCE)
        self.assertIn("ORB_HELP_MAX_TESTS      = 64", SOURCE)
        self.assertIn("ORB_HELP_BUDGET_US      = 30000", SOURCE)
        self.assertIn("PlayerSnap const orbAssistStart = snapFrom(p1);", SOURCE)
        self.assertIn("m_simSnap = &orbAssistStart;", SOURCE)
        self.assertIn("m_liveState.latest = -1.0;", SOURCE)
        self.assertIn("handing the click back to Assisted Click", SOURCE)
        self.assertIn("m_simUsedRingExact = false;", SOURCE)
        self.assertIn("m_simHeld = false;", SOURCE)
        self.assertIn("if (push && c.orbHelper && !touchingOrb(p1))", SOURCE)
        self.assertIn("state.canRingJump = p->m_stateRingJump2", SOURCE)
        self.assertIn("state.holdingJumpReleaseDelayed = p->m_stateJumpBuffered", SOURCE)
        self.assertIn("p->m_touchedRings.insert(r->m_uniqueID)", SOURCE)
        self.assertIn("p->m_touchingRings->removeObject(r, true)", SOURCE)
        self.assertNotIn("p->addToTouchedRings(r)", SOURCE)
        self.assertIn("bool const flying = p->isFlying();", SOURCE)
        self.assertIn("p->flipMod()", SOURCE)
        self.assertIn("if (r->m_isReverse)", SOURCE)
        self.assertIn("if (!simRingJump(player, ring)) return;", SOURCE)
        self.assertIn("m_simUsedRingExact = true;", SOURCE)
        self.assertIn("testExactTickDelayScheduling", (ROOT / "tests" / "orb_assist_tests.cpp").read_text(encoding="utf-8"))

    def test_orb_physics_covers_post_impulse_vehicle_rules(self):
        for required in (
            "factor = 0.37", "factor = 0.42", "factor = 0.77", "factor = 1.38",
            "out.flipAfterVelocity = true", "out.flipBeforeVelocity = true",
            "postSetVelocityMultiplier = 0.70", "postSetVelocityMultiplier = 0.60",
            "out.velocityUncapped = true", "Wave uses the ordinary ringJump path",
        ):
            self.assertIn(required, ORB_CORE)
        self.assertNotIn("if (in.vehicle == Vehicle::Wave)", ORB_CORE)

    def test_f6_cycles_five_modes_then_wraps(self):
        self.assertIn("int const next = (cur == STAGE_COUNT - 1) ? 0 : cur + 1;", SOURCE)
        self.assertEqual(MOD["settings"]["stage"]["one-of"], ["Off", "Stage 1", "Stage 2", "Stage 3", "Stage 4"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
