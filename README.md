# Geode Predictive Input Buffer — Orb Assist

Private development fork for **Geometry Dash 2.2081 / Geode 5.11.2** (Orb Assist v4.10.0). This branch improves Orb Assist while preserving the normal Assisted Click scheduling core.

## Orb Assist

When the player presses early near an orb, the mod evaluates two routes:

1. **Normal Assisted Click:** whether the click is safe immediately or its regular tick-delay search finds a safe input.
2. **Orb Assist:** uses a full `PlayerSnap` to discover the first approaching orb and its contact interval. Candidate ticks are simulated in chronological order. The planner stops at the **first complete safe result**; it no longer simulates every remaining tick merely to calculate a widest-run midpoint.

The normal route keeps priority if it is safe immediately. When both routes require delay, Orb Assist only replaces a confirmed normal plan if it has more measured collision clearance, or comparable clearance and an earlier tick. It does not infer safe-window width from sparse normal-search samples.

### Live verification

The click-time plan only estimates the contact interval. Beginning at the planned tick, the live path checks every physics tick inside that interval:

1. A one-step hidden-player probe requires Geometry Dash's native collision path to confirm the exact target orb on the upcoming physics step.
2. Only after that contact is confirmed, Orb Assist simulates the full judged post-orb route again from the current player snapshot.
3. The input is released only when both checks pass.

If the target is disabled, the contact interval expires, the current simulation is incomplete, or the live validation budget is exceeded, Orb Assist cancels and hands the input back to normal Assisted Click. When the main Prediction Stage is Off, it passes the input through instead.

The initial discovery/candidate search has an **8 ms CPU budget**; current-state post-orb validation has a **6 ms budget**. These are fail-closed upper budgets, not claims that every prediction takes that long. Early exit means the common case should test only the first few candidate ticks.

## Physics coverage and limits

The deterministic model follows Geometry Dash 2.2081 `PlayerObject::ringJump()` ordering, including `flipMod()`, mini scaling, pre-/post-impulse gravity flips, and the post-`setYVelocity` Ball/Spider/Swing multipliers.

Supported standard orb types: Yellow, Pink, Red, Black/Drop, Blue/Gravity and Green. The modeled vehicle set is Cube, Ship, Ball, UFO, Wave, Robot, Spider and Swing.

Orb Assist fails closed for unsupported or uncertain cases, including **confirmed moving orbs** (their future transforms are not yet modeled by the native contact probe), reverse orbs, custom/teleport/dash/spider orbs, unmodeled dynamic effects, incomplete simulations and exhausted budgets. An orb with no movement history is no longer rejected solely for being unclassified: exact upcoming contact and the post-orb path must still pass live validation.

It is a user-triggered input buffer, not an automatic clicker and not a guarantee that every death can be prevented.

## Controls

| Control | Action |
|---|---|
| F6 | Cycle Stage 1 → Stage 2 → Stage 3 → Stage 4 → Off |
| F7 | Open/close the optional Windows diagnostics console |
| Assist Orb Clicks setting | Enable/disable Orb Assist |

Stage Off disables the ordinary Assisted Click stage, not Orb Assist.

## Build

The repository GitHub Actions workflow builds the Windows mod using the pinned Geode SDK 5.11.2 and Geometry Dash 2.2081 bindings.

For local builds, use the Geode CLI:

```sh
geode build
```

## Validation status

A successful GitHub Actions build confirms compilation against the configured SDK. It does **not** replace live gameplay checks. Verify one-/two-tick contact, the live post-orb revalidation, fallback after an expired window, each standard orb type, all supported vehicles, x4 speed, mini, gravity changes, and respawn/checkpoint resets in Geometry Dash before relying on it in a level.

## References

- [Geometry Dash 2.2081 ringJump source](https://github.com/StarryDawn72/open-source-gd-project/blob/main/src/PlayerObject/ringJump.cpp)
- [Geometry Dash 2.2081 gravity-flip source](https://github.com/StarryDawn72/open-source-gd-project/blob/main/src/PlayerObject/flipGravity.cpp)
- [Geometry Dash 2.2081 jump physics source](https://github.com/StarryDawn72/open-source-gd-project/blob/main/src/PlayerObject/updateJump.cpp)
- [Lyoph GMD Buffer: contact-based orb buffering](https://github.com/Lyoph/GMD-Buffer-Mod)
- [Nova Input Buffer: plan once, recheck live](https://github.com/bockatta/GD-Input-Buffer)
