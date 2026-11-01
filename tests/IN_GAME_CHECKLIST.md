# In-game validation checklist (video profile)

Run these tests in Geometry Dash 2.2081 after the GitHub Actions package builds.

- [ ] Startup leaves the optional Windows console closed. Press F7 only when Orb Assist diagnostics are needed.
- [ ] Manually opened console shows a short version banner and Orb Assist decision lines only; routine scan and Assisted Click logs stay out of it.
- [ ] No Input Buffer window, pause-menu button, player badge, or path overlay appears.
- [ ] F6 cycles Stage 1 -> Stage 2 -> Stage 3 -> Stage 4 -> Off -> Stage 1; Orb Assist continues to operate independently of the normal Stage selection.
- [ ] Ordinary Assisted Click logs remain separate from the optional Orb Assist console.
- [ ] A click already predicted safe is passed through unchanged (no artificial press).
- [ ] Double taps and release timing remain ordered when a prior input is buffered.
- [ ] Ship and wave holds/releases are tested at 60, 144, 240 and high-TPS/CBF settings.
- [ ] Death, respawn, practice checkpoints, and level exit clear pending buffered inputs.
- [ ] Pausing a supported non-dual, non-platformer level starts the automatic scan without progress spam in the optional console.
- [ ] Scan stops/finishes without spawning path drawings or the original Input Buffer overlay.
- [ ] Test portal/orb/pad-heavy levels; unsupported predictions must fall back to raw input, not claim a save.
- [ ] F7 opens/closes the optional console without closing Geometry Dash.

## Important

Passing the standalone C++ unit tests is not proof of exact in-game prediction. Record any mismatch between console output and actual player behavior. A scan is advisory unless `Use Scanned Route (Experimental)` is enabled.

## Orb Assist regression matrix (v4.7.0)

- [ ] First encounter with each supported orb works without any previous calibration: Yellow, Pink, Red, Black/Drop, Blue/Gravity, Green.
- [ ] Verify all supported modes: Cube, Ship, Ball, UFO, Wave, Robot, Spider, Swing.
- [ ] Verify mini and normal size, normal and inverted gravity, and portals immediately before the orb.
- [ ] Try a click early by 1, 4, 12, 24 and 48 base ticks; successful Orb Assist attempts should produce one concise verification line when F7 is open.
- [ ] Try a one-tick safe contact at 240 TPS. The selected tick must be the exact tick found by simulation, not an approximate contact rectangle.
- [ ] Place two safe windows of different widths in the contact interval; Orb Assist must select the wider contiguous safe window.
- [ ] Use an orb whose contact is followed by a hazard within the judged path; Orb Assist must reject it unless the post-orb simulation survives.
- [ ] Disable/toggle the target orb before contact; Orb Assist must cancel and hand the input back to regular Assisted Click without forcing the target click.
- [ ] Test moving orbs / hazards and Move, Toggle, Spawn, Stop or other dynamic triggers. Moving or unclassified target orbs and suppressed effects must cause Orb Assist to skip, not claim a verified safe path.
- [ ] Test Ball / Spider (0.70 post-impulse velocity), Swing (0.60), and their automatic jump-release behavior.
- [ ] Test Dash, Gravity Dash, Spider Orb, Teleport Orb and custom orbs; they must be treated as unsupported by this deterministic model and must not borrow a standard-orb impulse.
- [ ] Test CPU exhaustion / dense levels. An incomplete search must not return a partial safe tick; regular Assisted Click remains available.
- [ ] Compare the real game with the console's claimed orb activation tick; record any tick mismatch before changing the model.

Run `python tests/run_checks.py` before each code handoff. These tests exercise the standalone physics model, exhaustive planner, original 666 core tests, and static integration checks; they do not replace the live checks above.
