# Orb Delay

Private development build for Geometry Dash 2.2081 and Geode 5.11.2.

Orb Delay evaluates a user's press near a supported stationary orb and compares the regular Assisted Click route against an exact Orb Assist simulation. It only delays for the orb when the predicted orb route has a safer result than the normal route.

The orb route uses a Level Scan player snapshot, maps the exact contact interval, tests every candidate physics tick, applies the 2.2081 ring impulse/gravity ordering, and checks the complete trajectory after activation. Live execution still requires the real player to touch the selected orb inside the tested safe interval. Missed or unsupported contacts are rejected rather than forced.

Supported standard orb types are Yellow, Pink, Red, Black/Drop, Blue/Gravity and Green. Moving, reverse, custom, teleport, dash and spider orbs, as well as unmodeled dynamic effects, remain unsupported and fail closed.

Orb Delay is input assistance, not an automatic clicker. A successful build does not prove live-game accuracy; in-game testing remains required.
