# Geode Predictive Input Buffer

**Unreleased personal-testing fork for Geometry Dash and Geode.** This project adapts the trajectory-simulation and input-scheduling engine from Input Buffer into a compact, keyboard-controlled workflow with a separate Windows command-prompt console.

## Overview

Geode Predictive Input Buffer uses simulated player trajectories to look for input timings that may avoid a predicted collision, then schedules eligible inputs through a configurable buffer. It also includes an optional scanned-route experiment for supported levels.

The mod is intended for local development and private testing. It is not an official Geometry Dash feature, and this project is not being presented as a public Geode release.

## Features

- **Prediction engine:** retains the supplied Input Buffer simulation and scheduling core.
- **Console-first workflow:** sends status and input-buffer messages to a separate Windows console instead of displaying the original Input Buffer overlay.
- **Prediction stages:** cycles through four timing profiles and an Off mode.
- **Automatic scan:** can begin scanning a compatible level when it is paused, using small processing slices while paused.
- **Optional scanned route:** experimental and disabled by default because scan results may be incomplete on complex or unsupported levels.
- **GitHub Actions build:** includes the project workflow for building against the configured Geode SDK.

## Controls

| Key | Action |
|---|---|
| `F6` | Cycle Stage 1 → Stage 2 → Stage 3 → Stage 4 → Off |
| `F7` | Close or reopen the Windows live console |

The stages correspond to the following base-tick limits:

| Stage | All inputs | Wave inputs |
|---|---:|---:|
| Stage 1 | 2 ticks | 2 ticks |
| Stage 2 | 4 ticks | 4 ticks |
| Stage 3 | 8 ticks | 8 ticks |
| Stage 4 | 16 ticks | 8 ticks |
| Off | No buffering | No buffering |

Actual simulation behavior depends on the game's detected physics timing and the supported mechanics in a level.

## Important limitations

This is a prediction tool, not a guarantee that every death can be prevented. Some inputs have no safe delay, and predicted routes can be inaccurate on levels with unsupported objects, complex portal sequences, dual gameplay, platformer mechanics, or other interactions the simulator does not fully model. The automatic scanner requires a stable paused level and does not scan every level type. The external command-prompt console is Windows-only.

A successful unit-test run validates the tested scheduling logic; it does **not** prove that collision prediction is always correct in live gameplay. Test changes carefully in Geometry Dash.

## Credits
To the me and the IA
## Build

Use the repository's GitHub Actions workflow to compile the source with the configured Geode SDK. See `README.md` for the build steps and test limitations.
