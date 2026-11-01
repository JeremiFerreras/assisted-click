e# 1.2.0

1.2 — Deterministic, fail-closed Orb Assist

- Replaced the Orb Assist calibration dependency with deterministic 2.2081 physics profiles for Yellow, Pink, Red, Black/Drop, Blue/Gravity, and Green orbs. The first encounter no longer depends on a previous successful activation or machine learning.
- Corrected mode-specific physics: Ship, UFO, Ball, Robot, Spider, Swing, Wave, mini scaling, inverted gravity, Blue/Green flip order, and the post-impulse 0.70 Ball/Spider and 0.60 Swing modifiers. The simulation also mirrors the relevant `ringJump` state flags (jump-used, jump-tick, jumping, last-grounded position, auto-jump eligibility, and uncapped velocity) and resets those flags from the real player before each candidate so probes cannot contaminate one another.
- Kept the new deterministic physics behind the Orb Assist simulation flag. The legacy Assisted Click predictor and `src/BufferCore.hpp` remain unchanged.
- Reworked Orb Assist to derive the contact interval from Geometry Dash's exact `m_touchingRings` callback, then test every candidate physics tick. Broadphase rectangles are not accepted as proof of contact.
- The planner selects the widest contiguous safe interval and its midpoint. A one-tick / frame-perfect solution is supported; incomplete simulations and CPU-budget exhaustion invalidate the whole search rather than returning a partial result.
- Orb Assist never forces a click just because its last candidate tick arrived. If GD has not confirmed the exact target orb, Orb Assist cancels its own plan and hands the still-pending input back to the regular Assisted Click safety check.
- Increased the look-ahead to 48 base ticks. The exact contact window is exhaustively searched up to 64 ticks; wider windows are rejected rather than coarsely sampled.
- Added 60 Hz real-game samples for nearby orbs and hazards while Orb Assist is enabled. The target orb must be confirmed stationary; moving or stale/unclassified orbs are rejected because GD's exact collision callback uses the live transform. Known move / effect triggers that the simulator suppresses also mark the candidate incomplete.
- Added an isolated C++ test suite for per-vehicle orb physics, gravity order, mini scaling, one-tick safe windows across the full 64-tick search, competing safe windows, missing exact contact, incomplete simulations, and CPU-budget aborts.

Verification note: the isolated Orb Assist suite and the existing core unit suite can be run without the Geode SDK. They verify deterministic formulas and planner behavior, not the complete Geometry Dash runtime. A successful Geode build and in-game validation are still required before calling the mod game-tested. Dash, Gravity Dash, Spider, Teleport, and custom orbs remain outside this first deterministic model and are never treated as supported Orb Assist targets.

# 1.1.0

1.1 — Calibrated orb-click assist

- Added the `Assist Orb Clicks` gameplay setting, enabled by default.
- The helper now discovers the target orb's full contact interval and probes each physics tick in that interval, rather than testing only the first contact tick.
- A candidate is accepted only when the simulator actually activates the target orb and its post-orb route is safe (or the remaining death is independently judged escapable).
- The helper uses saved real-game orb impulse samples keyed by orb type, vehicle, mini/normal size, and gravity direction. It does not guess a velocity for an unlearned vehicle/orb combination.
- The Level Scan portal/size/speed/gravity handling is reused during the search, and the next orb can serve as the next user-controlled decision in an orb chain.
- The live input waits until the chosen safe tick, checks the real orb hitbox, and never waits beyond the last fully tested safe tick. Console output reports the contact window, verified safe window, and last safe tick.
- The orb helper now also considers Wave, Ship, Robot, UFO, Swing, Cube, Ball, and Spider modes, using the learned input-hold rhythm when the vehicle's trajectory depends on release timing.

Verification note: the unit tests exercise the tick-window picker and configuration checks. They are not a substitute for testing every orb/vehicle pairing in the live Geometry Dash physics engine. A vehicle/orb pairing must first be learned from a real activation before the helper will delay it.

# 1.0.0
- Changelog — Geode Predictive Input Buffer

1.0 — First consolidated private adaptation
Status: private/local testing build. This is not an official Geometry Dash feature or an announced public Geode release.

Base: the supplied Input Buffer source package, whose existing code and manifest identify the upstream baseline as v4.6.0.
Version-number note: “1.0” labels the first consolidated feature set of this adaptation. The later 1.1 feature update advances the Geode manifest and startup console to v4.6.1; CMake's Template project version remains a build-project placeholder.

Added — separate live Windows console
- Added a Windows command-prompt console named Geode Predictive Input Buffer - Live Console as the main status and decision display.
- Console messages use the [InputBuffer] prefix and include startup information, the active prediction stage, stage-cycle guidance, scan status, and buffered-input decisions.
- Buffered decisions can report the amount of delay in a line such as SAVED +N TICKS, with frame/delay details and the applicable delay limit where the source logger has that information.
- Added F7 to close or reopen the external live console on Windows.
- Kept the console optional at runtime; the prediction code itself is not inherently Windows-only, but this separate CMD interface is.
Added — video-style prediction stages
- Added a predictable F6 cycle through four buffering stages and an Off mode:
  Mode	All-input window	Wave-input window
  Stage 1	2 base ticks	2 base ticks
  Stage 2	4 base ticks	4 base ticks
  Stage 3	8 base ticks	8 base ticks
  Stage 4	16 base ticks	8 base ticks
  Off	No buffering	No buffering
- The cycle is Stage 1 → Stage 2 → Stage 3 → Stage 4 → Off → Stage 1.
- The preset windows are expressed using the source simulator's 60-fps frame units and documented as equivalent base-tick limits at Geometry Dash's default 240-TPS timing. The effective physics steps can differ with the game's detected tick length and timing configuration.
- Added/retained the corresponding Geode settings for the selected stage, the F6 cycle key, and the F7 console key.
Adapted — prediction and input scheduling
- Retained the supplied trajectory-simulation and input-scheduling engine instead of replacing it with a simplified delay-only implementation.
- Preserved the engine's hidden simulation workflow for evaluating candidate input timings and looking for a delay that survives the simulator's predicted hazards.
- Preserved input scheduling behavior for supported press/release events, including the core's event ordering and replanning logic.
- Kept stage profiles as configuration around the existing engine, rather than copying only its UI or presenting unrelated settings from the original mod.
- The console reports what the mod's predictor/scheduler decided; it is diagnostic output, not independent proof that a save occurred in the live game.
Changed — original Input Buffer visuals disabled in this profile
The fork uses a console-first design. The original in-game Input Buffer interface is disabled in this profile, including:
- the original Input Buffer window/overlay;
- the pause-menu button used to open that window;
- the HUD/player badge;
- in-game trajectory/path and scan drawings;
- the original visible status/accuracy UI elements used by the upstream interface.
These elements are disabled to keep the in-game presentation aligned with the supplied video. This does not mean the underlying simulation, scanning, or scheduling code was rewritten from scratch.
Added — automatic paused-level scan
- Automatic Level Scan is enabled by default in the current settings.
- On an eligible level, the scan can start automatically when the player pauses, removing the need to initiate it manually.
- Scanning is processed in short slices, with a source-configured budget of approximately 2,000 microseconds (2 ms) per rendered frame while the level is paused.
- The console reports scan progress and completion instead of presenting a scan overlay in the game.
- An incomplete scan can be continued on a later eligible pause; keep the level paused while progress is being processed.
- The scanner requires a stable paused level. It is not intended to run its hidden scan workload during ordinary unpaused gameplay.
- Dual and platformer levels are excluded from this automatic scan path.
- Use Scanned Route (Experimental) is off by default. This option can let an early input follow a scanned route, but it remains experimental because scan results can be incomplete or incorrect around unsupported mechanics and complicated portal sequences.
Changed — project presentation and documentation
- Added the generated logo.png icon for the project.
- Added/updated DESCRIPTION.md with the purpose of the adaptation, controls, features, credits, and limitations.
- Updated README.md with the video-style profile, preset table, scanning behavior, GitHub Actions build instructions, and test caveats.
- Added this fork-specific CHANGELOG.md so the adaptation's changes are separate from the historical source changelog.
- Preserved the previously supplied changelog content in HISTORICAL_CHANGELOG.md; older descriptions of the original purple window, in-game buttons, or manual-scan UI refer to the source project and do not describe this fork's active interface.
Build and development workflow
- Retained the GitHub Actions workflow named Build Geode Mod so compilation can be run in GitHub rather than requiring VS Code as the user's build interface.
- The workflow is configured for Windows, macOS, Android 32-bit, Android 64-bit, and iOS targets and combines the platform builds into a .geode artifact when the workflow succeeds.
- The project metadata targets Geode 5.11.2 and Geometry Dash 2.2081.
- The source package contains the Geode CMakeLists.txt, mod metadata, source code, test sources, resources, and workflow configuration.
- The presence of a configured workflow does not itself prove every platform build succeeded; a successful Actions run is still required to verify the current checkout against the remote SDK/build environment.
Verification completed for this package
The following automated checks were run against the package while preparing this changelog:
- 6/6 static profile checks passed. These checks verify the manifest settings, stage values, expected console strings, disabled original UI elements, bounded automatic scan settings, and five-mode F6 cycle.
- 666/666 standalone core tests passed. These tests compile and run tests/core_tests.cpp against src/BufferCore.hpp without Geometry Dash or the Geode SDK.
These are useful checks of the tested code paths and configuration. They are not a proof of collision-prediction accuracy in a live level, do not replace a real Geode build, and do not prove that every death can be prevented.
Known limitations and required in-game validation
- The mod is a predictor, not a guarantee that every death can be prevented. Some input events have no safe delay within the selected window.
- Simulator results may be inaccurate on levels containing unsupported objects, complex portal sequences, orb/pad interactions, or other mechanics the simulator does not fully model.
- Dual and platformer levels are not handled by the automatic paused-level scan path.
- Scanned-route matching is experimental and deliberately off by default.
- The separate CMD console is Windows-only. Other configured targets do not receive an equivalent external Windows console.
- The included automated tests are synthetic/core/static tests. The in-game checklist still needs to be completed manually after a successful Geode build.
- Manual checks should include multiple frame rates and high-TPS/CBF configurations, ship and wave press/release behavior, double-taps, death/respawn, practice checkpoints, level exit, supported and unsupported object combinations, scan continuation, F6 stage transitions, and F7 console reopening.
Credits and attribution
- The trajectory simulation and input-scheduling engine is based on Input Buffer by AlQuerez23, from the source archive supplied for this private adaptation.
- This adaptation does not claim authorship of the original engine. Historical upstream notes are retained in HISTORICAL_CHANGELOG.md for context.
- This project is a private customization for local testing and is not presented as an official Geometry Dash feature or as an authorized publi
