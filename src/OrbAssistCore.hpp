#pragma once

// Stand-alone deterministic core for Orb Assist.
// This file deliberately has no Geode / Geometry Dash dependencies so its
// physics profiles and exhaustive tick planner can be tested in isolation.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace orbassist {

enum class Vehicle {
    Cube,
    Ship,
    Ball,
    Ufo,
    Wave,
    Robot,
    Spider,
    Swing,
    Unsupported,
};

enum class Orb {
    Yellow,
    Pink,
    Red,
    Black,
    Blue,
    Green,
    Unsupported,
};

struct PhysicsInput {
    Vehicle vehicle = Vehicle::Unsupported;
    Orb orb = Orb::Unsupported;
    double yStart = 0.0;
    bool mini = false;
    bool upsideDown = false;
    bool isFlying = false;
    // Native PlayerObject::flipMod() value when supplied by the game. Zero
    // keeps standalone tests/backwards-compatible callers on upsideDown.
    double nativeFlipMod = 0.0;
};

struct PhysicsEffect {
    bool valid = false;
    // Value passed to PlayerObject::setYVelocity (which rounds to 3 decimals).
    double yVelocity = 0.0;
    // ringJump multiplies m_yVelocity AFTER setYVelocity for Ball / Spider / Swing.
    double postSetVelocityMultiplier = 1.0;
    bool flipBeforeVelocity = false;
    bool flipAfterVelocity = false;
    bool releaseJump = false;
    bool velocityUncapped = false;
    // Ordinary jump rings and Black/Drop take different ringJump branches.
    // Only the ordinary branch marks the player as jumping / off-ground and
    // stores a new last-grounded position.
    bool standardJump = false;

    double finalYVelocity() const { return yVelocity * postSetVelocityMultiplier; }
};

inline double gdRoundVelocity(double value) {
    // 2.2081 PlayerObject::setYVelocity rounds to three decimal places.
    // Its implementation converts the integral part to int, so keep the
    // conversion defined even when this helper is called with hostile input.
    if (!std::isfinite(value) ||
        value <= static_cast<double>(std::numeric_limits<int>::min()) ||
        value >= static_cast<double>(std::numeric_limits<int>::max())) return value;
    double const intPart = static_cast<int>(value);
    if (intPart == value) return value;
    return intPart + std::round((value - intPart) * 1000.0) / 1000.0;
}

inline PhysicsEffect modelPhysics(PhysicsInput const& in) {
    PhysicsEffect out;
    if (in.vehicle == Vehicle::Unsupported || in.orb == Orb::Unsupported || !std::isfinite(in.yStart) ||
        in.yStart <= static_cast<double>(std::numeric_limits<int>::min()) ||
        in.yStart >= static_cast<double>(std::numeric_limits<int>::max())) {
        return out;
    }

    // Wave uses the ordinary ringJump path for standard rings. Its flying
    // flag is supplied separately for the Black / Drop impulse. Dash / Spider /
    // Teleport / Custom orbs are not represented by this enum and fail closed.

    double factor = 1.0;
    // Prefer GD's own flipMod() result. The fallback exists only for the
    // dependency-free test core and older call sites.
    double gravitySign = 0.0;
    if (std::isfinite(in.nativeFlipMod) && std::abs(in.nativeFlipMod) > 0.5) {
        gravitySign = in.nativeFlipMod < 0.0 ? -1.0 : 1.0;
    } else {
        gravitySign = in.upsideDown ? -1.0 : 1.0;
    }

    switch (in.orb) {
        case Orb::Yellow:
            if (in.vehicle == Vehicle::Robot) factor = 0.90;
            break;
        case Orb::Pink:
            switch (in.vehicle) {
                case Vehicle::Ship: factor = 0.37; break;
                case Vehicle::Ufo: factor = 0.42; break;
                case Vehicle::Ball: factor = 0.77; break;
                default: factor = 0.72; break;
            }
            break;
        case Orb::Red:
            switch (in.vehicle) {
                case Vehicle::Ship: factor = in.mini ? 1.40 : 1.00; break;
                case Vehicle::Ufo: factor = in.mini ? 1.36 : 1.02; break;
                case Vehicle::Ball: factor = 1.34; break;
                case Vehicle::Robot: factor = 1.28; break;
                case Vehicle::Spider: factor = 1.34; break;
                default: factor = 1.38; break;
            }
            out.velocityUncapped = true;
            break;
        case Orb::Blue:
            factor = 0.80;
            out.flipAfterVelocity = true;
            break;
        case Orb::Green:
            if (in.vehicle == Vehicle::Ship) factor = 0.70;
            out.flipBeforeVelocity = true;
            gravitySign = -gravitySign;
            break;
        case Orb::Black: {
            double const base = in.isFlying ? -14.0 : -15.0;
            double modeFactor = 1.0;
            if (in.vehicle == Vehicle::Spider && !in.isFlying) modeFactor = 1.10;
            if (in.vehicle == Vehicle::Ufo && in.isFlying) modeFactor = 0.80;
            out.yVelocity = gravitySign * base * modeFactor;
            out.releaseJump = in.vehicle == Vehicle::Ball || in.vehicle == Vehicle::Swing;
            out.velocityUncapped = true;
            out.standardJump = false;
            out.valid = true;
            out.yVelocity = gdRoundVelocity(out.yVelocity);
            return out;
        }
        case Orb::Unsupported:
            return out;
    }

    double const miniScale = in.mini ? 0.80 : 1.0;
    double const sign = gravitySign;
    // GD rounds setYVelocity first, then ringJump applies a mode-specific
    // multiplier for Ball / Spider / Swing. Keep that order: it is observable
    // at 240-TPS boundaries and differs from multiplying before rounding.
    out.yVelocity = gdRoundVelocity(sign * in.yStart * factor * miniScale);
    if (in.vehicle == Vehicle::Ball || in.vehicle == Vehicle::Spider) {
        out.postSetVelocityMultiplier = 0.70;
        out.releaseJump = true;
    } else if (in.vehicle == Vehicle::Swing) {
        out.postSetVelocityMultiplier = 0.60;
        out.releaseJump = true;
    }
    out.standardJump = true;
    out.valid = true;
    return out;
}

// Native 2.2081 PlayerObject::ringJump eligibility, expressed as a small
// value type so the exact gate can be tested without Geode. The field mapping
// is supplied by main.cpp from PlayerObject's live/simulated members.
struct ActivationState {
    bool alive = true;
    bool canRingJump = true;                 // m_canRingJump
    bool dashing = false;                    // m_isDashing
    bool holdingJumpReleaseDelayed = true;   // m_holdingJumpReleaseDelayed
    bool alreadyActivated = false;           // activatedRingIDs contains uniqueID
    bool touchedRing = false;                // m_touchedRing
    bool touchedCustomRing = false;          // m_touchedCustomRing
    bool touchedTeleportRing = false;        // m_touchedTeleportRing
    bool isJumpingRing = true;
    bool isCustomRing = false;
    bool isTeleportRing = false;
};

inline bool canActivate(const ActivationState& state) {
    if (!state.alive || !state.canRingJump || state.dashing ||
        !state.holdingJumpReleaseDelayed || state.alreadyActivated) {
        return false;
    }

    // This mirrors the native ringJump gate. Supported Orb Assist types are
    // ordinary jumping rings, but keeping the complete condition prevents a
    // future custom/teleport extension from silently changing GD's rules.
    if ((state.touchedRing || !state.isJumpingRing) &&
        (state.touchedCustomRing || !state.isCustomRing)) {
        if (state.touchedTeleportRing) return false;
        if (!state.isTeleportRing) return false;
    }
    return true;
}

struct TickProbe {
    // `complete` means the game simulation covered the full judged path and
    // did not cross an unmodelled dynamic trigger / effect.
    bool complete = false;
    bool exactTargetContact = false;
    bool survived = false;
    float clearance = 0.0f;
};

enum class PlanStatus {
    Found,
    InvalidRange,
    WindowTooWide,
    BudgetExpired,
    IncompleteSimulation,
    NoSafeTick,
};

struct TickPlan {
    PlanStatus status = PlanStatus::InvalidRange;
    int best = -1;
    int runLo = -1;
    int runHi = -1;
    int tested = 0;
    int safeTicks = 0;
    float clearance = -std::numeric_limits<float>::infinity();
};

// Compare confirmed candidate plans. The normal route wins if it is already
// safe now. For a delayed normal route, Orb Assist must improve measured
// clearance or reach an equally safe solution earlier; unmeasured window width
// is never guessed from sparse samples.
inline bool preferOrbPlan(bool normalImmediateSafe, bool normalDelayedSafe,
                          int normalDelay, float normalClearance,
                          int orbDelay, float orbClearance,
                          float clearanceMargin = 0.5f) {
    if (normalImmediateSafe || orbDelay <= 0) return false;
    if (!normalDelayedSafe) return true;
    if (orbClearance > normalClearance + clearanceMargin) return true;
    return std::fabs(orbClearance - normalClearance) <= clearanceMargin &&
           normalDelay > 0 && orbDelay < normalDelay;
}

// Test candidate ticks in chronological order and stop at the FIRST fully
// verified safe tick. Orb Assist only needs the earliest safe activation; it
// does not need to map the width of every safe run or calculate a midpoint.
// This removes the old full-window sweep after a valid answer was already found.
//
// maxWindowTicks / maxTests bound worst-case work. If the search is capped
// before reaching hi, return WindowTooWide rather than falsely claiming that
// no safe tick exists. A timeout or incomplete simulation invalidates the
// candidate; partial winners are never returned.
template <class Probe>
TickPlan chooseEarliestSafeTick(int lo, int hi, int maxWindowTicks, int maxTests, Probe&& probe) {
    TickPlan plan;
    if (hi < lo || maxWindowTicks < 1 || maxTests < 1) {
        plan.status = PlanStatus::InvalidRange;
        return plan;
    }

    long long const total = static_cast<long long>(hi) - static_cast<long long>(lo) + 1LL;
    int const limit = static_cast<int>(std::min<long long>(
        total, std::min(maxWindowTicks, maxTests)
    ));

    for (int i = 0; i < limit; ++i) {
        int const tick = lo + i;
        std::optional<TickProbe> result = probe(tick);
        if (!result) {
            plan.status = PlanStatus::BudgetExpired;
            return plan;
        }

        ++plan.tested;
        if (!result->complete) {
            plan.status = PlanStatus::IncompleteSimulation;
            return plan;
        }

        if (result->exactTargetContact && result->survived) {
            plan.status = PlanStatus::Found;
            plan.best = tick;
            plan.runLo = tick;
            plan.runHi = tick;
            plan.safeTicks = 1;
            plan.clearance = result->clearance;
            return plan;
        }
    }

    plan.status = (static_cast<long long>(limit) < total)
        ? PlanStatus::WindowTooWide
        : PlanStatus::NoSafeTick;
    return plan;
}

} // namespace orbassist
