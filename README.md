# Geode Predictive Input Buffer — Orb Delay

Private development fork for **Geometry Dash 2.2081 / Geode 5.11.2**. The current work focuses on Orb Delay while preserving the existing Assisted Click scheduling core.

## Orb Delay

When the player presses near a supported stationary orb, the mod evaluates two possible routes:

1. **Normal Assisted Click:** whether the click is safe immediately, or whether its regular tick-delay search finds a safe delayed input.
2. **Orb Assist:** uses a full Level Scan `PlayerSnap` of the current player, maps the orb's approach/contact interval, then simulates every candidate tick in that interval. A candidate counts only if Geometry Dash's simulated ring callback confirms the exact target orb and the complete post-orb route survives.

The planner selects the widest contiguous safe tick interval; its midpoint is the preferred tick. The normal route keeps priority if it is safe immediately. When both routes require delay, Orb Assist only replaces a confirmed normal plan if it has more measured collision clearance, or comparable clearance and an earlier tick. It does not infer safe-window width from sparse normal-search samples.

At runtime, the selected tick is scheduled on the game's physics-time grid and the live player must confirm contact with that exact orb. If the orb is unavailable or the verified contact window is missed, Orb Assist cancels and passes the pending input back rather than forcing an unverified activation. Orb Delay remains available when the regular Prediction Stage is Off.

## Physics coverage and limits

The deterministic model follows Geometry Dash 2.2081 `PlayerObject::ringJump()` ordering, including `flipMod()`, mini scaling, pre-/post-impulse gravity flips, and the post-`setYVelocity` Ball/Spider/Swing multipliers.

Supported standard orb types: Yellow, Pink, Red, Black/Drop, Blue/Gravity and Green. The modeled vehicle set is Cube, Ship, Ball, UFO, Wave, Robot, Spider and Swing.

Orb Delay fails closed for unsupported or uncertain cases, including moving orbs, reverse orbs, custom/teleport/dash/spider orbs, unmodeled dynamic effects, incomplete simulations and exhausted search budgets. It is a user-triggered input buffer, not an automatic clicker and not a guarantee that every death can be prevented.

## Controls

| Control | Action |
|---|---|
| F6 | Cycle Stage 1 → Stage 2 → Stage 3 → Stage 4 → Off |
| F7 | Open/close the optional Windows diagnostics console |
| Assist Orb Clicks setting | Enable/disable Orb Delay |

Stage Off disables the ordinary Assisted Click stage, not Orb Delay.

## Build

The repository GitHub Actions workflow builds the Windows mod using the pinned Geode SDK 5.11.2 and Geometry Dash 2.2081 bindings.

For local builds, use the Geode CLI:

```sh
geode build
```

## Validation status

A successful GitHub Actions build confirms compilation against the configured SDK. It does **not** replace live gameplay checks. In particular, verify frame-perfect one-tick contact, missed-contact fallback, each standard orb type, all supported vehicles, and respawn/checkpoint resets in Geometry Dash before relying on it in a level.

## References

- [Geometry Dash 2.2081 ringJump source](https://github.com/StarryDawn72/open-source-gd-project/blob/main/src/PlayerObject/ringJump.cpp)
- [Geometry Dash 2.2081 gravity-flip source](https://github.com/StarryDawn72/open-source-gd-project/blob/main/src/PlayerObject/flipGravity.cpp)
- [Geometry Dash 2.2081 jump physics source](https://github.com/StarryDawn72/open-source-gd-project/blob/main/src/PlayerObject/updateJump.cpp)
