# Orb Assist

Private development build for Geometry Dash 2.2081 and Geode 5.11.2.

Orb Assist compares the normal Assisted Click result with a predicted orb route. It finds the first fully simulated safe contact tick and stops searching immediately; it does not spend time mapping every safe interval or selecting a midpoint.

Before sending the buffered input, the live path confirms the exact target orb on the upcoming native collision step, then simulates the post-orb route again from the current player snapshot. If either check fails, the click remains buffered only inside the bounded contact window and is then returned to normal Assisted Click (or passed through when Prediction Stage is Off).

Supported standard orb types: Yellow, Pink, Red, Black/Drop, Blue/Gravity and Green. Supported vehicle model: Cube, Ship, Ball, UFO, Wave, Robot, Spider and Swing. Confirmed moving orbs, reverse/custom/teleport/dash/spider orbs, unmodeled dynamic effects, incomplete simulations and exhausted budgets fail closed. Missing movement samples alone do not reject an orb; exact live contact and post-orb validation are still required.

Prediction budgets: 8 ms for initial discovery/candidate search and 6 ms for current-state live route validation. These are hard fail-closed budgets, not measured average latency.

Orb Assist is a user-triggered input buffer, not an automatic clicker. A successful build does not prove live-game accuracy; in-game testing remains required.
