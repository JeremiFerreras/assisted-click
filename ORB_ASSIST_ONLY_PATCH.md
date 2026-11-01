# Orb Assist-only patch for v4.7.0

This patch is based on the supplied `assisted-click-orb-assist-v4.7.0.zip`.
It is intentionally limited to Orb Assist behavior and its isolated tests.

## Changes

- Keeps Orb Assist isolated behind `m_orbAssistSimulation` in the simulated ring path.
- Checks the same native 2.2081 ring activation preconditions before accepting a simulated activation: alive state, `m_canRingJump`, dash state, delayed jump-release state, activated ring ID, and the touched-ring/custom/teleport state combination.
- Uses `PlayerObject::isFlying()` and `PlayerObject::flipMod()` for native flight/gravity-direction state instead of reconstructing those values from parallel booleans.
- Uses GD's exact player-scale comparison (`m_vehicleSize != 1.0f`) for mini scaling.
- Mirrors native activation bookkeeping for the supported standard rings: records the activated unique ID and removes the ring from the simulated player's current touching-ring list.
- Rejects reverse rings until the `reversePlayer()` side effect is modelled. This is deliberately fail-closed rather than simulating an incorrect normal-orb impulse.
- Adds isolated tests for native flip direction, native activation eligibility, and an exact 16-tick input delay.

## Assisted Click isolation

- `src/BufferCore.hpp` is unchanged byte-for-byte from the supplied v4.7.0 project.
- The existing learned-orb path remains the non-Orb-Assist branch of `simRingJump()`.
- `orbPress()` and `applyGdRing()` preserve the previous event/bookkeeping order when `m_orbAssistSimulation` is false.
- No normal Assisted Click timing, settings, planner, or queue implementation was changed.

## Validation

`python tests/run_checks.py` passed:

- Core tests: 666 passed, 0 failed.
- Orb Assist isolated tests: 359 checks, 0 failures.
- Video/profile static checks: 8 passed, 0 failed.

These checks do not substitute for compiling against the target Geode SDK or testing in Geometry Dash. The native-state mapping and ring behavior are based on the 2.2081 implementation; validate the compiled build in the matching game/Geode environment before relying on it in live levels.

## v4.7.0 Level Scan contact discovery

- Level Scan's full `PlayerSnap` seeds all Orb Assist probes.
- A fast discovery-only pass maps a candidate orb contact interval with Level Scan's object geometry; it stops once the interval closes.
- The actual delayed tick still requires exact Geometry Dash ring contact and a surviving post-orb trajectory. Geometric overlap alone cannot fire a click.
- The normal Assisted Click queue/core is left intact; Orb Assist uses a fixed game-time queue and is not re-timed by the live planner.
- Windows console is opt-in and has no automatic startup/scan chatter.
