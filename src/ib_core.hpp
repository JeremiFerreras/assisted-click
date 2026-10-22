#pragma once
// Prediction engine. This is the GMD Buffer (Lyophi, MIT) logic with the
// settings removed: one "stage" picks the window for every mode, the same
// way the video mod works. It has no Geode / Windows dependency; main.cpp
// supplies a Sink that talks to the game, and tests/ supplies a fake one.
//
// Sink must provide:
//   void send(int idx, bool down, bool groundOnly);
//   void saved(int idx, Mode mode, bool onRing, int ticksSaved,
//              int frame0, int delayed, int limit);
//
// The engine counts "steps". The video mod assumes the default 240 TPS, so
// 1 tick == 1 step and the window in steps equals the window in ticks.

#include "ib_format.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace ib {

enum class Mode { Cube = 0, Ball, Robot, Spider, Ship, Ufo, Swing, Wave };

inline constexpr int SPAWN_GRACE_STEPS = 16;

struct PlayerInfo {
    Mode mode = Mode::Cube;
    bool onGround = false;
    int rings = 0; // orbs currently overlapping the player

    bool isFly() const {
        return mode == Mode::Ship || mode == Mode::Ufo || mode == Mode::Swing || mode == Mode::Wave;
    }
    bool isGround() const { return !isFly(); }
    bool isWave() const { return mode == Mode::Wave; }
};

inline char const* modeName(Mode m) {
    switch (m) {
        case Mode::Cube: return "cube";
        case Mode::Ball: return "ball";
        case Mode::Robot: return "robot";
        case Mode::Spider: return "spider";
        case Mode::Ship: return "ship";
        case Mode::Ufo: return "ufo";
        case Mode::Swing: return "swing";
        case Mode::Wave: return "wave";
    }
    return "cube";
}

struct PState {
    bool held = false;
    bool pending = false;
    int pendingStart = 0;
    bool releaseNext = false;

    int pressStep = -1;
    bool delayedRelease = false;
    int releaseAt = 0;

    std::vector<std::pair<bool, int>> waveQueue; // (down, applyAtStep)
    int lastReleaseStep = -1000000;
};

class Core {
public:
    int stage = 0; // 0..4, starts at STAGE 1 like the video
    int step = 0;
    int graceUntil = 0;
    PState ps[2];

    // ---- windows -------------------------------------------------------
    int limitFor(Mode m) const {
        return m == Mode::Wave ? STAGE_WAVE[stage] : STAGE_ALL[stage];
    }
    int waveSteps() const { return STAGE_WAVE[stage]; }

    void cycleStage() { stage = (stage + 1) % STAGE_COUNT; }
    void beginStep() { ++step; }

    // Death / restart / checkpoint. Returns true when we were holding back a
    // release the game never saw, so the caller must release the button.
    bool reset() {
        bool inconsistent = false;
        for (auto& s : ps) {
            if (s.delayedRelease || s.releaseNext || !s.waveQueue.empty()) inconsistent = true;
            s = PState();
        }
        graceUntil = step + SPAWN_GRACE_STEPS;
        return inconsistent;
    }

    void clearAll() {
        for (auto& s : ps) s = PState();
    }

    // ---- per-step ------------------------------------------------------
    template <class Sink>
    void stepPlayer(Sink& sink, int idx, PlayerInfo const& p) {
        PState& s = ps[idx];

        if (!s.waveQueue.empty()) {
            if (!p.isWave()) {
                flushWave(sink, idx);
            } else {
                size_t applied = 0;
                while (applied < s.waveQueue.size() && step >= s.waveQueue[applied].second) {
                    sink.send(idx, s.waveQueue[applied].first, false);
                    applied++;
                }
                s.waveQueue.erase(s.waveQueue.begin(), s.waveQueue.begin() + applied);
            }
        }

        if (s.delayedRelease) {
            if (!p.isFly() || step >= s.releaseAt) {
                s.delayedRelease = false;
                s.held = false;
                sink.send(idx, false, false);
            }
        }

        if (s.releaseNext) {
            s.releaseNext = false;
            if (!s.held) sink.send(idx, false, true);
        }

        if (!s.pending) return;

        int waited = step - s.pendingStart;
        int limit = limitFor(p.mode);

        if (limit <= 0 || waited > limit || !p.isGround()) {
            s.pending = false;
            return;
        }

        bool onRing = p.rings > 0;
        if (p.onGround || onRing) {
            s.pending = false;
            if (!s.held) {
                sink.send(idx, true, true);
                s.releaseNext = true;
                sink.saved(idx, p.mode, onRing, waited, s.pendingStart, step, limit);
            }
        }
    }

    // Returns true when the engine swallowed the event (the game must NOT
    // see it now). false = pass it to the game as usual.
    template <class Sink>
    bool input(Sink& sink, int idx, PlayerInfo const& p, bool down) {
        PState& s = ps[idx];

        if (!p.isWave() && !s.waveQueue.empty()) flushWave(sink, idx);

        int n = waveSteps();
        bool inGrace = step < graceUntil;

        if (p.isWave() && n > 0 && !inGrace) {
            int target = ((step + n - 1) / n) * n; // next grid line
            if (!s.waveQueue.empty()) {
                auto const& last = s.waveQueue.back();
                target = std::max(target, last.second);
                // A tap must last at least one cell or press and release
                // would cancel out on the same step.
                if (!down && last.first) target = std::max(target, last.second + n);
            }
            s.held = down;
            s.waveQueue.emplace_back(down, target);
            return true;
        }

        if (down && s.delayedRelease) {
            s.delayedRelease = false;
            s.held = false;
            sink.send(idx, false, false);
        }

        if (!down && p.isFly() && s.pressStep >= 0) {
            int minHold = limitFor(p.mode);
            int heldFor = step - s.pressStep;
            if (minHold > 0 && heldFor < minHold) {
                s.delayedRelease = true;
                s.releaseAt = s.pressStep + minHold;
                return true;
            }
        }

        s.held = down;

        if (down) {
            s.pressStep = p.isFly() ? step : -1;
            bool early = p.isGround() && !p.onGround && p.rings == 0;
            if (early && limitFor(p.mode) > 0) {
                s.pending = true;
                s.pendingStart = step;
            } else {
                s.pending = false;
            }
        }
        return false;
    }

private:
    template <class Sink>
    void flushWave(Sink& sink, int idx) {
        PState& s = ps[idx];
        for (auto const& ev : s.waveQueue) sink.send(idx, ev.first, false);
        s.waveQueue.clear();
    }
};

} // namespace ib
