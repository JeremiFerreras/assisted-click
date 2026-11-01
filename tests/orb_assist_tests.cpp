#include "../src/OrbAssistCore.hpp"
#include "../src/BufferCore.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

using namespace orbassist;

namespace {
int checks = 0;
int failures = 0;

void check(bool condition, char const* label) {
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", label);
    }
}

void near(double actual, double expected, char const* label) {
    check(std::fabs(actual - expected) < 0.0011, label);
}

void testPhysicsProfiles() {
    // 2.2081 reference: yellow / pink / red and gravity orbs use yStart.
    PhysicsEffect e = modelPhysics({Vehicle::Cube, Orb::Yellow, 10.0, false, false, false});
    check(e.valid, "cube yellow valid"); near(e.yVelocity, 10.0, "cube yellow impulse");

    e = modelPhysics({Vehicle::Cube, Orb::Pink, 10.0, false, false, false});
    check(e.valid, "cube pink valid"); near(e.yVelocity, 7.2, "cube pink factor 0.72");

    e = modelPhysics({Vehicle::Cube, Orb::Red, 10.0, false, false, false});
    check(e.valid && e.velocityUncapped && e.standardJump, "cube red standard jump and uncapped");
    near(e.yVelocity, 13.8, "cube red factor 1.38");

    e = modelPhysics({Vehicle::Ship, Orb::Pink, 10.0, false, false, true});
    near(e.yVelocity, 3.7, "ship pink factor 0.37");
    e = modelPhysics({Vehicle::Ufo, Orb::Pink, 10.0, false, false, true});
    near(e.yVelocity, 4.2, "ufo pink factor 0.42");
    e = modelPhysics({Vehicle::Ball, Orb::Pink, 10.0, false, false, false});
    near(e.finalYVelocity(), 5.39, "ball pink factor 0.77 then 0.70 mode multiplier");
    e = modelPhysics({Vehicle::Robot, Orb::Yellow, 10.0, false, false, false});
    near(e.yVelocity, 9.0, "robot yellow factor 0.90");
    e = modelPhysics({Vehicle::Spider, Orb::Red, 10.0, false, false, false});
    near(e.finalYVelocity(), 9.38, "spider red factor 1.34 then 0.70 mode multiplier");
    e = modelPhysics({Vehicle::Swing, Orb::Red, 10.0, false, false, false});
    near(e.finalYVelocity(), 8.28, "swing red factor 1.38 then 0.60 mode multiplier");

    e = modelPhysics({Vehicle::Ship, Orb::Red, 10.0, true, false, true});
    near(e.yVelocity, 11.2, "mini ship red factor and 0.8 scale");
    e = modelPhysics({Vehicle::Ufo, Orb::Red, 10.0, true, false, true});
    near(e.yVelocity, 10.88, "mini ufo red factor and 0.8 scale");

    // Gravity ordering matters: green flips before velocity, blue after it.
    e = modelPhysics({Vehicle::Cube, Orb::Green, 10.0, false, false, false});
    check(e.flipBeforeVelocity && !e.flipAfterVelocity, "green flips before velocity");
    near(e.yVelocity, -10.0, "green uses post-flip gravity sign");
    e = modelPhysics({Vehicle::Cube, Orb::Blue, 10.0, false, false, false});
    check(e.flipAfterVelocity && !e.flipBeforeVelocity, "blue flips after velocity");
    near(e.yVelocity, 8.0, "blue uses pre-flip gravity sign");
    e = modelPhysics({Vehicle::Cube, Orb::Blue, 10.0, false, true, false});
    near(e.yVelocity, -8.0, "blue upside-down velocity");
    // Live Orb Assist supplies PlayerObject::flipMod(); when present it must
    // override the parallel cached orientation flag rather than re-derive it.
    e = modelPhysics({Vehicle::Cube, Orb::Yellow, 10.0, false, false, false, -1.0});
    near(e.yVelocity, -10.0, "native flipMod overrides cached upright flag");
    e = modelPhysics({Vehicle::Cube, Orb::Green, 10.0, false, true, false, -1.0});
    near(e.yVelocity, 10.0, "green orb inverts native flipMod before impulse");
    e = modelPhysics({Vehicle::Ship, Orb::Green, 10.0, false, false, true});
    near(e.yVelocity, -7.0, "ship green factor 0.7 after flip");

    // Drop / black orbs use fixed impulses, and mode-specific release effects.
    e = modelPhysics({Vehicle::Cube, Orb::Black, 0.0, false, false, false});
    check(!e.standardJump && e.velocityUncapped, "black/drop uses distinct ringJump branch and uncapped velocity");
    near(e.yVelocity, -15.0, "cube black impulse -15");
    e = modelPhysics({Vehicle::Ship, Orb::Black, 0.0, false, false, true});
    near(e.yVelocity, -14.0, "ship black impulse -14");
    e = modelPhysics({Vehicle::Ufo, Orb::Black, 0.0, false, false, true});
    near(e.yVelocity, -11.2, "ufo black impulse -14 times 0.8");
    e = modelPhysics({Vehicle::Spider, Orb::Black, 0.0, false, false, false});
    near(e.yVelocity, -16.5, "spider black impulse -15 times 1.1");
    e = modelPhysics({Vehicle::Ball, Orb::Black, 0.0, false, false, false});
    check(e.releaseJump, "ball black releases jump");
    e = modelPhysics({Vehicle::Swing, Orb::Black, 0.0, false, false, true});
    check(e.releaseJump, "swing black releases jump");
    e = modelPhysics({Vehicle::Spider, Orb::Pink, 10.0, false, false, false});
    check(e.releaseJump, "spider pink releases jump");
    e = modelPhysics({Vehicle::Ball, Orb::Red, 10.0, false, false, false});
    check(e.releaseJump, "ball red releases jump");
    near(e.finalYVelocity(), 9.38, "ball red applies 0.70 after red factor");
    e = modelPhysics({Vehicle::Swing, Orb::Pink, 10.0, false, false, true});
    near(e.finalYVelocity(), 4.32, "swing pink applies 0.60 after pink factor");
    e = modelPhysics({Vehicle::Ball, Orb::Yellow, 1.234, false, false, false});
    near(e.yVelocity, 1.234, "ball setYVelocity value is rounded before mode scaling");
    near(e.finalYVelocity(), 0.8638, "ball post-set scale is not rounded a second time");

    // Mini uses the separate 0.8 player-scale modifier; inversion flips the sign.
    e = modelPhysics({Vehicle::Cube, Orb::Yellow, 10.0, true, false, false});
    near(e.yVelocity, 8.0, "mini yellow 0.8 scale");
    e = modelPhysics({Vehicle::Cube, Orb::Yellow, 10.0, false, true, false});
    near(e.yVelocity, -10.0, "inverted yellow sign");

    e = modelPhysics({Vehicle::Wave, Orb::Yellow, 10.0, false, false, true});
    check(e.valid, "wave yellow uses standard jump-ring path");
    near(e.yVelocity, 10.0, "wave yellow impulse");
    e = modelPhysics({Vehicle::Wave, Orb::Red, 10.0, false, false, true});
    check(e.valid, "wave red uses standard jump-ring path");
    near(e.yVelocity, 13.8, "wave red impulse");
    e = modelPhysics({Vehicle::Wave, Orb::Black, 0.0, false, false, true});
    check(e.valid, "wave black/drop uses flying impulse");
    near(e.yVelocity, -14.0, "wave black/drop impulse");
    check(!modelPhysics({Vehicle::Unsupported, Orb::Yellow, 10.0, false, false, false}).valid,
          "unknown vehicle fails closed");
    check(!modelPhysics({Vehicle::Cube, Orb::Unsupported, 10.0, false, false, false}).valid,
          "unknown orb fails closed");
    check(!modelPhysics({Vehicle::Cube, Orb::Yellow, std::numeric_limits<double>::quiet_NaN(), false, false, false}).valid,
          "non-finite yStart fails closed");
    check(!modelPhysics({Vehicle::Cube, Orb::Yellow, 1.0e100, false, false, false}).valid,
          "out-of-range yStart fails closed before GD int rounding");
    near(gdRoundVelocity(1.0e100), 1.0e100,
         "rounding helper avoids undefined int conversion for huge finite input");

    // Exhaustively cover the supported mode/orb matrix. This is deliberately
    // deterministic: no profile learning or repeated gameplay attempts.
    constexpr Vehicle vehicles[] = {Vehicle::Cube, Vehicle::Ship, Vehicle::Ball, Vehicle::Ufo,
                                    Vehicle::Wave, Vehicle::Robot, Vehicle::Spider, Vehicle::Swing};
    constexpr Orb orbs[] = {Orb::Yellow, Orb::Pink, Orb::Red, Orb::Black, Orb::Blue, Orb::Green};
    for (Vehicle vehicle : vehicles) {
        for (Orb orb : orbs) {
            PhysicsEffect const profile = modelPhysics({vehicle, orb, 9.375, false, false,
                vehicle == Vehicle::Ship || vehicle == Vehicle::Ufo ||
                vehicle == Vehicle::Wave || vehicle == Vehicle::Swing});
            check(profile.valid, "supported mode/orb pair has deterministic profile");
            check(std::isfinite(profile.finalYVelocity()), "supported mode/orb profile remains finite");
        }
    }
}

void testExactTickDelayScheduling() {
    TickPlan const plan = chooseSafeTick(0, 63, 64, 64, [](int tick) -> std::optional<TickProbe> {
        return TickProbe{true, true, tick == 16, tick == 16 ? 5.0f : 0.0f};
    });
    check(plan.status == PlanStatus::Found && plan.best == 16,
          "Orb Assist can select an exact 16-tick plan");

    // Orb Assist converts the chosen tick to game time and hands the offset
    // to the same queue. At 240 TPS, the event must not fire on tick 15 and
    // must be due on tick 16 (4 frames at 60 FPS).
    constexpr double dt = 1.0 / 240.0;
    ibcore::InputQueue queue;
    queue.start(true, 0.0, static_cast<double>(plan.best) * dt);
    check(!queue.poll(15.0 * dt).has_value(), "16-tick delay does not fire at tick 15");
    auto const due = queue.poll(16.0 * dt);
    check(due.has_value() && *due, "16-tick delay fires exactly at tick 16");
}

void testNativeActivationGate() {
    ActivationState s;
    check(canActivate(s), "fresh ordinary orb matches GD ringJump eligibility");

    s.alive = false;
    check(!canActivate(s), "dead player cannot activate an orb");
    s = ActivationState{};
    s.canRingJump = false;
    check(!canActivate(s), "GD canRingJump flag blocks activation");
    s = ActivationState{};
    s.dashing = true;
    check(!canActivate(s), "dashing player cannot activate an ordinary orb");
    s = ActivationState{};
    s.holdingJumpReleaseDelayed = false;
    check(!canActivate(s), "GD delayed-held-input flag is required");
    s = ActivationState{};
    s.alreadyActivated = true;
    check(!canActivate(s), "an already activated unique ring ID is rejected");
    s = ActivationState{};
    s.touchedRing = true;
    check(!canActivate(s), "GD touched-ring state blocks a second ordinary ring");
    s = ActivationState{};
    s.touchedRing = true;
    s.touchedTeleportRing = true;
    check(!canActivate(s), "touched teleport state cannot pass the native ring gate");

    // Explicit custom/teleport cases guard the full native condition so a
    // future extension cannot weaken the existing eligibility rules.
    s = ActivationState{};
    s.isJumpingRing = false;
    s.isCustomRing = true;
    check(canActivate(s), "first custom ring passes the matching native gate");
    s.touchedRing = true;
    check(canActivate(s), "custom ring retains the native exception after an ordinary ring");
    s.touchedCustomRing = true;
    check(!canActivate(s), "a previously touched custom ring blocks another custom ring");
    s = ActivationState{};
    s.isJumpingRing = false;
    s.isTeleportRing = true;
    s.touchedRing = true;
    s.touchedCustomRing = true;
    check(canActivate(s), "teleport orb is the exception to native touched-ring gate");
    s.touchedTeleportRing = true;
    check(!canActivate(s), "already touched teleport orb blocks a second teleport orb");
}

void testExhaustiveTickPlanner() {
    // At 240 physics ticks per second, each candidate is one exact tick.
    // Every single-tick safe window is found at every position in the
    // 64-tick interval; no sampling stride is allowed to skip a frame-perfect
    // window, and every candidate is tested exactly once.
    for (int safe = 0; safe < 64; ++safe) {
        int calls = 0;
        TickPlan p = chooseSafeTick(0, 63, 64, 64, [&](int d) -> std::optional<TickProbe> {
            ++calls;
            return TickProbe{true, true, d == safe, d == safe ? 4.0f : 0.0f};
        });
        check(p.status == PlanStatus::Found, "single tick plan found");
        check(p.best == safe && p.runLo == safe && p.runHi == safe, "single tick exact position");
        check(p.tested == 64 && calls == 64, "every tick tested exactly once");
    }

    // The widest safe interval wins, not the first one sampled.
    TickPlan p = chooseSafeTick(0, 50, 64, 64, [](int d) -> std::optional<TickProbe> {
        bool const safe = (d == 0) || (d >= 18 && d <= 27);
        return TickProbe{true, true, safe, d == 0 ? 100.0f : 7.0f};
    });
    check(p.status == PlanStatus::Found && p.runLo == 18 && p.runHi == 27 && p.best == 22,
          "widest safe window wins even when a smaller one appears first");

    // Equal-width ranges prefer the stronger minimum clearance.
    p = chooseSafeTick(0, 20, 32, 32, [](int d) -> std::optional<TickProbe> {
        bool const a = d >= 2 && d <= 4;
        bool const b = d >= 12 && d <= 14;
        return TickProbe{true, true, a || b, b ? 9.0f : 2.0f};
    });
    check(p.status == PlanStatus::Found && p.runLo == 12 && p.runHi == 14,
          "clearance breaks equal-window tie");
    check(std::fabs(p.clearance - 9.0f) < 0.001f,
          "selected safe run reports its weakest clearance");

    // Any unknown physics / out-of-budget result invalidates the whole search,
    // even if a previous tick looked safe: no partial plan may leak out.
    int calls = 0;
    p = chooseSafeTick(0, 10, 16, 16, [&](int d) -> std::optional<TickProbe> {
        ++calls;
        if (d == 6) return std::nullopt;
        return TickProbe{true, true, d == 1, 10.0f};
    });
    check(p.status == PlanStatus::BudgetExpired && p.best == -1, "budget abort drops partial winner");
    check(calls == 7, "budget abort is immediate");

    p = chooseSafeTick(0, 10, 16, 16, [](int d) -> std::optional<TickProbe> {
        if (d == 5) return TickProbe{false, false, true, 99.0f};
        return TickProbe{true, true, d == 2, 10.0f};
    });
    check(p.status == PlanStatus::IncompleteSimulation && p.best == -1,
          "unmodelled dynamic segment aborts all candidates");

    p = chooseSafeTick(0, 64, 64, 64, [](int) -> std::optional<TickProbe> {
        return TickProbe{true, true, true, 1.0f};
    });
    check(p.status == PlanStatus::WindowTooWide && p.tested == 0,
          "overwide windows rejected without partial sampling");

    p = chooseSafeTick(0, 10, 16, 16, [](int) -> std::optional<TickProbe> {
        return TickProbe{true, false, true, 10.0f};
    });
    check(p.status == PlanStatus::NoSafeTick && p.best == -1,
          "broadphase contact without exact target contact never qualifies");
}
}

int main() {
    testPhysicsProfiles();
    testExactTickDelayScheduling();
    testNativeActivationGate();
    testExhaustiveTickPlanner();
    std::printf("Orb Assist isolated tests: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
