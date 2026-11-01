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
    bool gravitySign = in.upsideDown;

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
            gravitySign = !gravitySign;
            break;
        case Orb::Black: {
            double const base = in.isFlying ? -14.0 : -15.0;
            double modeFactor = 1.0;
            if (in.vehicle == Vehicle::Spider && !in.isFlying) modeFactor = 1.10;
            if (in.vehicle == Vehicle::Ufo && in.isFlying) modeFactor = 0.80;
            out.yVelocity = (in.upsideDown ? -1.0 : 1.0) * base * modeFactor;
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
    double const sign = gravitySign ? -1.0 : 1.0;
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
};

// Every tick in the contact interval is simulated. A partial result is never
// accepted: timeouts or incomplete physics invalidate the whole candidate set.
template <class Probe>
TickPlan chooseSafeTick(int lo, int hi, int maxWindowTicks, int maxTests, Probe&& probe) {
    TickPlan plan;
    if (hi < lo || maxWindowTicks < 1 || maxTests < 1) {
        plan.status = PlanStatus::InvalidRange;
        return plan;
    }
    int const count = hi - lo + 1;
    if (count > maxWindowTicks || count > maxTests) {
        plan.status = PlanStatus::WindowTooWide;
        return plan;
    }

    struct Entry { bool safe = false; float clearance = 0.0f; };
    std::vector<Entry> entries(static_cast<std::size_t>(count));

    for (int d = lo; d <= hi; ++d) {
        std::optional<TickProbe> result = probe(d);
        if (!result) {
            plan.status = PlanStatus::BudgetExpired;
            plan.best = plan.runLo = plan.runHi = -1;
            return plan;
        }
        ++plan.tested;
        if (!result->complete) {
            plan.status = PlanStatus::IncompleteSimulation;
            plan.best = plan.runLo = plan.runHi = -1;
            return plan;
        }
        bool const safe = result->exactTargetContact && result->survived;
        entries[static_cast<std::size_t>(d - lo)] = Entry{safe, result->clearance};
        if (safe) ++plan.safeTicks;
    }

    int bestLo = -1, bestHi = -1, bestLen = 0;
    float bestMargin = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < count;) {
        if (!entries[static_cast<std::size_t>(i)].safe) { ++i; continue; }
        int const start = i;
        float margin = std::numeric_limits<float>::infinity();
        while (i < count && entries[static_cast<std::size_t>(i)].safe) {
            margin = std::min(margin, entries[static_cast<std::size_t>(i)].clearance);
            ++i;
        }
        int const end = i - 1;
        int const length = end - start + 1;
        if (length > bestLen || (length == bestLen && margin > bestMargin)) {
            bestLen = length;
            bestLo = lo + start;
            bestHi = lo + end;
            bestMargin = margin;
        }
    }

    if (bestLen == 0) {
        plan.status = PlanStatus::NoSafeTick;
        return plan;
    }
    plan.status = PlanStatus::Found;
    plan.runLo = bestLo;
    plan.runHi = bestHi;
    plan.best = bestLo + (bestHi - bestLo) / 2;
    return plan;
}

} // namespace orbassist
