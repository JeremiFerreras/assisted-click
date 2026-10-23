# In-game validation checklist (video profile)

Run these tests in Geometry Dash 2.2081 after the GitHub Actions package builds.

- [ ] Startup creates a separate Windows console titled `Geode Predictive Input Buffer - Live Console`.
- [ ] Startup output includes version, selected mode, and the F6 stage sequence.
- [ ] No Input Buffer window, pause-menu button, player badge, or path overlay appears.
- [ ] F6 cycles Stage 1 -> Stage 2 -> Stage 3 -> Stage 4 -> Off -> Stage 1; each transition is printed to the console.
- [ ] A press/release saved by prediction produces a `[InputBuffer] SAVED +N TICKS (.. frames @ 60fps) PRESS! (Frame: .. -> Delayed (..)) [Limit: ..]` line.
- [ ] A click already predicted safe is passed through unchanged (no artificial press).
- [ ] Double taps and release timing remain ordered when a prior input is buffered.
- [ ] Ship and wave holds/releases are tested at 60, 144, 240 and high-TPS/CBF settings.
- [ ] Death, respawn, practice checkpoints, and level exit clear pending buffered inputs.
- [ ] Pausing a supported non-dual, non-platformer level starts the automatic scan and logs progress every 10%.
- [ ] Scan stops/finishes without spawning path drawings or the original Input Buffer overlay.
- [ ] Test portal/orb/pad-heavy levels; unsupported predictions must fall back to raw input, not claim a save.
- [ ] F7 closes and reopens the console without closing Geometry Dash.

## Important

Passing the standalone C++ unit tests is not proof of exact in-game prediction. Record any mismatch between console output and actual player behavior. A scan is advisory unless `Use Scanned Route (Experimental)` is enabled.
