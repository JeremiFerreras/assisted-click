// Assisted Click - stable input queue and diagnostic console
// Target: Geometry Dash 2.2081 / Geode 5.11.2 / Windows
//
// IMPORTANT:
// This file deliberately does NOT pretend to run the ShowTrajectory physics
// predictor. The supplied project archive does not contain the full PlayerData /
// PlayerPracticeFixes state-snapshot implementation required for reliable clone
// simulation. This stage gives the input queue, ordering, F6 control, reset handling,
// and console diagnostics a clean base. See PREDICTOR_INTEGRATION.md.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/ui/Notification.hpp>

#if defined(_WIN32)
#include <Windows.h>
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

using namespace geode::prelude;

namespace ac {

// -----------------------------------------------------------------------------
// Windows console
// -----------------------------------------------------------------------------
namespace console {
#if defined(_WIN32)
    HANDLE g_output = INVALID_HANDLE_VALUE;
    bool g_open = false;
#else
    bool g_open = false;
#endif
    bool g_hasStatus = false;
    std::string g_lastStatus;
    constexpr std::size_t kStatusWidth = 100;

    void writeRaw(std::string_view value) {
#if defined(_WIN32)
        if (!g_open || g_output == INVALID_HANDLE_VALUE || value.empty()) {
            return;
        }
        DWORD written = 0;
        WriteConsoleA(
            g_output,
            value.data(),
            static_cast<DWORD>(value.size()),
            &written,
            nullptr
        );
#else
        (void)value;
#endif
    }

    void drawStatus() {
        if (!g_open || g_lastStatus.empty()) {
            return;
        }

        std::string line = g_lastStatus;
        if (line.size() > kStatusWidth) {
            line.resize(kStatusWidth);
        }
        if (line.size() < kStatusWidth) {
            line.append(kStatusWidth - line.size(), ' ');
        }

        writeRaw("\r");
        writeRaw(line);
        writeRaw("\r");
        g_hasStatus = true;
    }

    void init() {
#if defined(_WIN32)
        if (g_open) {
            return;
        }

        // Geode normally launches without a console. Create a dedicated one.
        FreeConsole();
        if (!AllocConsole()) {
            return;
        }

        SetConsoleTitleA("Assisted Click - Diagnostics");
        g_output = GetStdHandle(STD_OUTPUT_HANDLE);
        g_open = (g_output != INVALID_HANDLE_VALUE && g_output != nullptr);
        if (!g_open) {
            return;
        }

        writeRaw("Assisted Click - diagnostic console\r\n");
        writeRaw("F6 cycles the configured delay: 0 / 2 / 4 / 8 / 16 ticks.\r\n");
        writeRaw("This build is the stable queue base; physics prediction is not active yet.\r\n");
        writeRaw("\r\n");
#endif
    }

    void event(std::string_view message) {
        if (!g_open) {
            return;
        }

        // The live tick status occupies one line. Erase it before printing an event,
        // then redraw it below the new event so normal ticks do not flood the console.
        if (g_hasStatus) {
            writeRaw("\r");
            std::string blanks(kStatusWidth, ' ');
            writeRaw(blanks);
            writeRaw("\r");
        }

        writeRaw("[AC] ");
        writeRaw(message);
        writeRaw("\r\n");
        drawStatus();
    }

    void tick(int currentTick, int configuredDelay, std::size_t queueSize) {
        if (!g_open) {
            return;
        }

        g_lastStatus = "Tick=" + std::to_string(currentTick)
            + " | configured delay=" + std::to_string(configuredDelay)
            + " | queued inputs=" + std::to_string(queueSize)
            + " | predictor=NOT CONNECTED";
        drawStatus();
    }
}

// -----------------------------------------------------------------------------
// Input queue
// -----------------------------------------------------------------------------
struct PendingInput {
    std::uint64_t sequence = 0;
    int queuedAtTick = 0;
    int applyAtTick = 0;
    bool down = false;
    int button = 0;
    bool isPlayer1 = true;
};

constexpr int kDelayOptions[] = { 0, 2, 4, 8, 16 };
constexpr std::size_t kDelayOptionCount = sizeof(kDelayOptions) / sizeof(kDelayOptions[0]);

std::deque<PendingInput> g_queue;
int g_tick = 0;
std::size_t g_delayIndex = 0;
std::uint64_t g_nextSequence = 1;
bool g_applyingQueuedInput = false;

int configuredDelay() {
    return kDelayOptions[g_delayIndex];
}

void clearPendingInputs(const char* reason) {
    const auto oldSize = g_queue.size();
    g_queue.clear();
    g_tick = 0;
    g_nextSequence = 1;

    if (oldSize > 0) {
        console::event(
            std::string("QUEUE CLEARED: ") + reason
            + " | discarded=" + std::to_string(oldSize)
        );
    } else {
        console::event(std::string("LEVEL RESET: ") + reason);
    }
}

// A real game-layer tick is counted only for the currently active PlayLayer,
// not for another GJBaseGameLayer instance that may briefly exist elsewhere.
bool isActivePlayLayer(GJBaseGameLayer* layer) {
    auto* playLayer = PlayLayer::get();
    return playLayer && static_cast<GJBaseGameLayer*>(playLayer) == layer;
}

struct ApplyingGuard {
    bool previous = false;
    ApplyingGuard() : previous(g_applyingQueuedInput) {
        g_applyingQueuedInput = true;
    }
    ~ApplyingGuard() {
        g_applyingQueuedInput = previous;
    }
};

void enqueueInput(bool down, int button, bool isPlayer1) {
    const int delay = configuredDelay();

    int dueTick = g_tick + delay;

    if (!g_queue.empty()) {
        // Preserve chronological input order. In OFF mode, a new event must not
        // overtake an event that is already buffered.
        if (delay == 0) {
            dueTick = std::max(g_tick + 1, g_queue.back().applyAtTick);
        } else {
            dueTick = std::max(dueTick, g_queue.back().applyAtTick);
        }
    }

    PendingInput pending;
    pending.sequence = g_nextSequence++;
    pending.queuedAtTick = g_tick;
    pending.applyAtTick = dueTick;
    pending.down = down;
    pending.button = button;
    pending.isPlayer1 = isPlayer1;
    g_queue.push_back(pending);

    console::event(
        std::string("INPUT QUEUED #") + std::to_string(pending.sequence)
        + " | " + (down ? "PRESS" : "RELEASE")
        + " | button=" + std::to_string(button)
        + " | now=" + std::to_string(g_tick)
        + " | due=" + std::to_string(dueTick)
        + " | delay=" + std::to_string(delay)
        + " | queue=" + std::to_string(g_queue.size())
    );
}

} // namespace ac

// -----------------------------------------------------------------------------
// F6: cycle delay option
// -----------------------------------------------------------------------------
class $modify(ACKeyboardDispatcher, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double timestamp) {
        if (key == enumKeyCodes::KEY_F6 && down && !repeat) {
            ac::g_delayIndex = (ac::g_delayIndex + 1) % ac::kDelayOptionCount;
            const int delay = ac::configuredDelay();

            ac::console::event(
                std::string("F6: configured delay changed to ")
                + std::to_string(delay) + " ticks"
            );

            Notification::create(
                fmt::format("Configured delay: {} ticks", delay),
                NotificationIcon::None,
                1.0f
            )->show();
        }

        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
    }
};

// -----------------------------------------------------------------------------
// Reset state before the original level reset runs. This prevents old queued
// events from leaking into a restarted attempt.
// -----------------------------------------------------------------------------
class $modify(ACPlayLayer, PlayLayer) {
    void resetLevel() {
        ac::clearPendingInputs("PlayLayer::resetLevel");
        PlayLayer::resetLevel();
    }
};

// -----------------------------------------------------------------------------
// Queue dispatch and click interception
// -----------------------------------------------------------------------------
class $modify(ACBaseGameLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (ac::isActivePlayLayer(this)) {
            ++ac::g_tick;

            while (!ac::g_queue.empty()
                && ac::g_queue.front().applyAtTick <= ac::g_tick) {
                const ac::PendingInput pending = ac::g_queue.front();
                ac::g_queue.pop_front();

                // The guard makes this path safe even if the call routes through
                // the modified handleButton method on this Geode build.
                {
                    ac::ApplyingGuard guard;
                    GJBaseGameLayer::handleButton(
                        pending.down,
                        pending.button,
                        pending.isPlayer1
                    );
                }

                ac::console::event(
                    std::string("INPUT APPLIED #") + std::to_string(pending.sequence)
                    + " | " + (pending.down ? "PRESS" : "RELEASE")
                    + " | queuedAt=" + std::to_string(pending.queuedAtTick)
                    + " | due=" + std::to_string(pending.applyAtTick)
                    + " | appliedAt=" + std::to_string(ac::g_tick)
                    + " | waited=" + std::to_string(ac::g_tick - pending.queuedAtTick)
                    + " | remaining=" + std::to_string(ac::g_queue.size())
                );
            }

            ac::console::tick(
                ac::g_tick,
                ac::configuredDelay(),
                ac::g_queue.size()
            );
        }

        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        // Player 2 is intentionally untouched. In Geometry Dash's signature,
        // isPlayer1 == true identifies player 1.
        if (!isPlayer1 || ac::g_applyingQueuedInput || !PlayLayer::get() || !m_player1) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        const int delay = ac::configuredDelay();

        // Zero delay is true pass-through when there is nothing waiting. If the
        // queue is non-empty, preserve order rather than letting a new input jump it.
        if (delay == 0 && ac::g_queue.empty()) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        ac::enqueueInput(down, button, isPlayer1);
    }
};

$on_mod(Loaded) {
    ac::console::init();
    ac::console::event("MOD LOADED");
}
