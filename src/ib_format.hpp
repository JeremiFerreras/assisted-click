#pragma once
// Pure text helpers (no Geode / Windows). Every string the console prints is
// built here so it can be unit-tested against the lines seen in the video.

#include <cstdio>
#include <string>

namespace ib {

inline constexpr int STAGE_COUNT = 5;
// Window per stage, in ticks. Index 4 = OFF.
inline constexpr int STAGE_ALL[STAGE_COUNT]  = {2, 4, 8, 16, 0};
inline constexpr int STAGE_WAVE[STAGE_COUNT] = {2, 4, 8, 8, 0};

inline constexpr char const* CONSOLE_TITLE = "Geode Predictive Input Buffer - Live Console";
inline constexpr char const* VERSION_TAG   = "v4.6";

namespace detail {
inline std::string fmt1(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", v);
    return b;
}
inline std::string fmt2(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.2f", v);
    return b;
}
} // namespace detail

inline std::string separatorLine() { return std::string(60, '='); }

// " [InputBuffer] v4.6 LOADED (Wave Spikes/Saws Trajectory Fix)"
inline std::string loadedLine() {
    return std::string(" [InputBuffer] ") + VERSION_TAG + " LOADED (Wave Spikes/Saws Trajectory Fix)";
}

// " [InputBuffer] Starting Mode: STAGE 1 (2 All / 2 Wave)"
inline std::string startingModeLine(int stage) {
    return " [InputBuffer] Starting Mode: STAGE " + std::to_string(stage + 1) + " (" +
           std::to_string(STAGE_ALL[stage]) + " All / " + std::to_string(STAGE_WAVE[stage]) + " Wave)";
}

inline std::string hintLine() {
    return " [InputBuffer] Press F6 in-level to cycle: 2/2 -> 4/4 -> 8/8 -> 16/8 -> OFF";
}

// "[InputBuffer] F6 Cycle -> STAGE 2: 4 Ticks All | 4 Ticks Wave (1.0f @ 60fps)"
// "[InputBuffer] F6 Cycle -> STAGE 4: 16 Ticks All | 8 Ticks Wave (4.0f / 2.0f @ 60fps)"
// "[InputBuffer] F6 Cycle -> STAGE 5: OFF (100% Raw Input)"
inline std::string cycleLine(int stage) {
    std::string s = "[InputBuffer] F6 Cycle -> STAGE " + std::to_string(stage + 1) + ": ";
    int all = STAGE_ALL[stage];
    int wave = STAGE_WAVE[stage];
    if (all == 0) return s + "OFF (100% Raw Input)";
    s += std::to_string(all) + " Ticks All | " + std::to_string(wave) + " Ticks Wave (";
    if (all == wave) {
        s += detail::fmt1(wave / 4.0) + "f @ 60fps)";
    } else {
        s += detail::fmt1(all / 4.0) + "f / " + detail::fmt1(wave / 4.0) + "f @ 60fps)";
    }
    return s;
}

// "[InputBuffer] *** SAVED +2 TICKS (0.50 frames @ 60fps) PRESS! (Frame0: 8 -> Delayed: 65) [Limit: 4] ***"
inline std::string savedLine(int ticksSaved, int frame0, int delayed, int limit) {
    return "[InputBuffer] *** SAVED +" + std::to_string(ticksSaved) + " TICKS (" +
           detail::fmt2(ticksSaved / 4.0) + " frames @ 60fps) PRESS! (Frame0: " +
           std::to_string(frame0) + " -> Delayed: " + std::to_string(delayed) +
           ") [Limit: " + std::to_string(limit) + "] ***";
}

} // namespace ib
