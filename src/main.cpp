#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <mutex>
#include <string>

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/modify/EffectGameObject.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace geode::prelude;

namespace ac_console {

#ifdef _WIN32

static std::mutex g_mutex;
static HANDLE g_output = INVALID_HANDLE_VALUE;
static bool g_ready = false;
static bool g_bannerShown = false;
static bool g_tickLineActive = false;
static size_t g_tickLineLength = 0;

static void writeRaw(const std::string& text) {
    if (g_output == nullptr ||
        g_output == INVALID_HANDLE_VALUE ||
        text.empty()) {
        return;
    }

    DWORD written = 0;

    WriteConsoleA(
        g_output,
        text.data(),
        static_cast<DWORD>(text.size()),
        &written,
        nullptr
    );
}

bool init() {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (g_ready) {
        return true;
    }

    // Separarse de cualquier consola heredada o existente y forzar una nueva.
    FreeConsole();

    if (!AllocConsole()) {
        return false;
    }

    g_output = GetStdHandle(STD_OUTPUT_HANDLE);

    if (g_output == nullptr ||
        g_output == INVALID_HANDLE_VALUE) {
        FreeConsole();
        return false;
    }

    SetConsoleTitleA("Assisted Click - Live Console");
    SetConsoleOutputCP(CP_UTF8);

    g_ready = true;
    return true;
}

void line(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_ready) {
        return;
    }

    if (g_tickLineActive) {
        writeRaw("\r\n");
        g_tickLineActive = false;
        g_tickLineLength = 0;
    }

    writeRaw(message);
    writeRaw("\r\n");
}

void tickLine(std::string message) {
    std::lock_guard<std::mutex> lock(g_mutex);

    if (!g_ready) {
        return;
    }

    if (message.size() > 110) {
        message.resize(110);
    }

    if (message.size() < g_tickLineLength) {
        message.append(
            g_tickLineLength - message.size(),
            ' '
        );
    }

    writeRaw("\r");
    writeRaw(message);

    g_tickLineLength = message.size();
    g_tickLineActive = true;
}

void showBannerOnce() {
    if (!init()) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);

        if (g_bannerShown) {
            return;
        }

        g_bannerShown = true;
    }

    line("===================================================");
    line("[Assisted Click] v1.0.0 LOADED");
    line("[Assisted Click] Live console: direct game events");
    line("[Assisted Click] Tick status updates in place");
    line("[Assisted Click] Press F6 in-level to cycle prediction window");
    line("===================================================");
}

#else

inline bool init() {
    return false;
}

inline void line(const std::string&) {}

inline void tickLine(std::string) {}

inline void showBannerOnce() {}

#endif

} // namespace ac_console

// Apertura automática al cargar el mod mediante la API de Geode
$on_mod(Loaded) {
    ac_console::showBannerOnce();
}

namespace ac {

    constexpr float kPhysicsDt = 1.0f / 240.0f;
    constexpr int kLookAhead = 32;

    const int kWindows[] = {0, 2, 4, 8, 16};
    int g_windowIndex = 0;

    int g_tick = 0;
    float g_lastDx = 0.0f;

    PlayLayer* g_layer = nullptr;
    PlayerObject* g_ghost = nullptr;

    bool g_simulating = false;
    bool g_cloneDied = false;
    bool g_triggerEncountered = false;
    bool g_applyingQueuedInput = false;

    struct PendingInput {
        int tick;
        bool down;
        int button;
        bool player2;
    };

    std::deque<PendingInput> g_queue;
    std::unordered_map<int, int> g_pressDelay;

    int windowTicks() {
        return kWindows[g_windowIndex];
    }

    int buttonKey(int button, bool player2) {
        return button + (player2 ? 10000 : 0);
    }

    template <class P>
    void seedGhost(P* dst, P* src) {
        dst->resetCollisionLog(true);
        dst->resetCollisionValues();

        dst->setPosition(src->getPosition());
        dst->setRotation(src->getRotation());
        dst->setScale(src->getScale());

        #define AC_COPY(F) \
            if constexpr (requires { dst->F = src->F; }) { \
                dst->F = src->F; \
            }

        AC_COPY(m_position)
        AC_COPY(m_lastPosition)
        AC_COPY(m_yVelocity)
        AC_COPY(m_fallSpeed)
        AC_COPY(m_playerSpeed)
        AC_COPY(m_speedMultiplier)
        AC_COPY(m_platformerXVelocity)
        AC_COPY(m_physDeltaRelated)

        AC_COPY(m_isShip)
        AC_COPY(m_isBird)
        AC_COPY(m_isBall)
        AC_COPY(m_isDart)
        AC_COPY(m_isRobot)
        AC_COPY(m_isSpider)
        AC_COPY(m_isSwing)
        AC_COPY(m_isUpsideDown)
        AC_COPY(m_isOnGround)
        AC_COPY(m_isOnGround2)
        AC_COPY(m_isOnGround4)
        AC_COPY(m_isOnSlope)
        AC_COPY(m_wasOnSlope)
        AC_COPY(m_isGoingLeft)
        AC_COPY(m_isSideways)
        AC_COPY(m_isPlatformer)
        AC_COPY(m_isSecondPlayer)
        AC_COPY(m_controlsDisabled)

        AC_COPY(m_holdingButtons)
        AC_COPY(m_holdingRight)
        AC_COPY(m_holdingLeft)
        AC_COPY(m_jumpBuffered)
        AC_COPY(m_wasJumpBuffered)
        AC_COPY(m_stateJumpBuffered)
        AC_COPY(m_lastGroundedPos)
        AC_COPY(m_lastLandTime)
        AC_COPY(m_lastJumpTime)
        AC_COPY(m_lastFlipTime)
        AC_COPY(m_totalTime)
        AC_COPY(m_gameModeChangedTime)
        AC_COPY(m_hasEverJumped)

        #undef AC_COPY

        if constexpr (requires { dst->m_isDead = false; }) {
            dst->m_isDead = false;
        }

        if constexpr (requires { dst->m_playEffects = false; }) {
            dst->m_playEffects = false;
        }
    }

    struct ActivationBits {
        bool hasIsActivated = false;
        bool isActivated = false;

        bool hasActivatedP1 = false;
        bool activatedP1 = false;

        bool hasActivatedP2 = false;
        bool activatedP2 = false;
    };

    template <class O>
    ActivationBits captureActivation(O* obj) {
        ActivationBits state;

        if constexpr (requires { obj->m_isActivated; }) {
            state.hasIsActivated = true;
            state.isActivated = obj->m_isActivated;
        }

        if constexpr (requires { obj->m_hasBeenActivated; }) {
            state.hasActivatedP1 = true;
            state.activatedP1 = obj->m_hasBeenActivated;
        }

        if constexpr (requires { obj->m_hasBeenActivatedP2; }) {
            state.hasActivatedP2 = true;
            state.activatedP2 = obj->m_hasBeenActivatedP2;
        }

        return state;
    }

    template <class O>
    void restoreActivation(O* obj, const ActivationBits& state) {
        if constexpr (requires { obj->m_isActivated = false; }) {
            if (state.hasIsActivated) {
                obj->m_isActivated = state.isActivated;
            }
        }

        if constexpr (requires { obj->m_hasBeenActivated = false; }) {
            if (state.hasActivatedP1) {
                obj->m_hasBeenActivated = state.activatedP1;
            }
        }

        if constexpr (requires { obj->m_hasBeenActivatedP2 = false; }) {
            if (state.hasActivatedP2) {
                obj->m_hasBeenActivatedP2 = state.activatedP2;
            }
        }
    }

    struct ActivationGuard {
        std::vector<std::pair<GameObject*, ActivationBits>> saved;

        explicit ActivationGuard(PlayLayer* layer) {
            for (auto* object : CCArrayExt<GameObject*>(layer->m_objects)) {
                if (object) {
                    saved.emplace_back(
                        object,
                        captureActivation(object)
                    );
                }
            }
        }

        ~ActivationGuard() {
            for (auto& entry : saved) {
                if (entry.first) {
                    restoreActivation(entry.first, entry.second);
                }
            }
        }
    };

    struct SimulationScope {
        SimulationScope() {
            g_simulating = true;
            g_cloneDied = false;
            g_triggerEncountered = false;
        }

        ~SimulationScope() {
            g_simulating = false;
        }
    };

    template <class P>
    bool cloneOnSlope(P* player) {
        if constexpr (requires { player->m_isOnSlope; }) {
            return player->m_isOnSlope;
        }

        return false;
    }

    void applyGhostInput(bool down, int button) {
        if (!g_ghost) {
            return;
        }

        auto playerButton = static_cast<PlayerButton>(button);

        if (down) {
            g_ghost->pushButton(playerButton);
        }
        else {
            g_ghost->releaseButton(playerButton);
        }
    }

    bool simulateDelay(
        PlayLayer* layer,
        PlayerObject* real,
        bool down,
        int button,
        int delay
    ) {
        if (!layer || !real || !g_ghost) {
            return false;
        }

        ActivationGuard activationGuard(layer);
        bool survived = true;

        {
            SimulationScope scope;
            seedGhost(g_ghost, real);

            const int totalSteps = delay + kLookAhead;

            for (int step = 0; step < totalSteps; ++step) {
                if (step == delay) {
                    applyGhostInput(down, button);
                }

                const float beforeX = g_ghost->getPositionX();

                g_ghost->update(kPhysicsDt);

                const float afterX = g_ghost->getPositionX();

                if (std::abs(afterX - beforeX) < 0.0001f &&
                    std::abs(g_lastDx) > 0.0001f) {
                    g_ghost->setPositionX(beforeX + g_lastDx);
                }

                const float collisionDt =
                    cloneOnSlope(g_ghost) ? kPhysicsDt : 0.0f;

                layer->checkCollisions(
                    g_ghost,
                    collisionDt,
                    true
                );

                if (g_cloneDied || g_triggerEncountered) {
                    survived = false;
                    break;
                }
            }
        }

        return survived;
    }

    int predictDelay(
        PlayLayer* layer,
        PlayerObject* player,
        bool down,
        int button,
        bool player2
    ) {
        const int limit = windowTicks();

        if (!layer || !player || !g_ghost ||
            player2 || limit <= 0 || g_tick < 2) {
            return 0;
        }

        const bool originalSafe =
            simulateDelay(layer, player, down, button, 0);

        if (originalSafe) {
            return 0;
        }

        for (int delay = 1; delay <= limit; ++delay) {
            const bool safe = simulateDelay(
                layer, player, down, button, delay
            );

            if (safe) {
                return delay;
            }
        }

        return 0;
    }

    void queueInput(
        PlayLayer* layer,
        bool down,
        int button,
        bool player2,
        int delay
    ) {
        if (!layer) {
            return;
        }

        if (delay > 0) {
            ac_console::line(
                "[Assisted Click] DELAY SELECTED: +" +
                std::to_string(delay) +
                " TICKS | " +
                (down ? "PRESS" : "RELEASE") +
                " | tick " +
                std::to_string(ac::g_tick) +
                " -> due " +
                std::to_string(ac::g_tick + delay) +
                " | limit " +
                std::to_string(windowTicks())
            );
        }

        if (delay == 0 && g_queue.empty()) {
            g_applyingQueuedInput = true;
            layer->handleButton(down, button, player2);
            g_applyingQueuedInput = false;
            return;
        }

        int dueTick = g_tick + delay;

        if (!g_queue.empty()) {
            dueTick = std::max(
                dueTick,
                g_queue.back().tick
            );

            if (!down) {
                dueTick = std::max(
                    dueTick,
                    g_queue.back().tick + 1
                );
            }
        }

        g_queue.push_back({
            dueTick,
            down,
            button,
            player2
        });
    }

    void dispatchPendingInputs(PlayLayer* layer, int nextTick) {
        while (!g_queue.empty() &&
               g_queue.front().tick <= nextTick) {
            auto input = g_queue.front();
            g_queue.pop_front();

            g_applyingQueuedInput = true;
            layer->handleButton(
                input.down,
                input.button,
                input.player2
            );
            g_applyingQueuedInput = false;
        }
    }

    void clearState() {
        g_queue.clear();
        g_pressDelay.clear();
        g_tick = 0;
        g_lastDx = 0.0f;

        g_cloneDied = false;
        g_triggerEncountered = false;
        g_applyingQueuedInput = false;
    }

} // namespace ac

class $modify(ACKeyboard, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(
        cocos2d::enumKeyCodes key,
        bool down,
        bool repeat,
        double timestamp
    ) {
        if (key == cocos2d::enumKeyCodes::KEY_F6 && down && !repeat) {
            ac::g_windowIndex =
                (ac::g_windowIndex + 1) % 5;

            const int currentWindow = ac::windowTicks();

            if (currentWindow <= 0) {
                ac_console::line(
                    "[Assisted Click] F6 Cycle -> OFF (raw input)"
                );
            } else {
                ac_console::line(
                    "[Assisted Click] F6 Cycle -> " +
                    std::to_string(currentWindow) +
                    " Ticks prediction"
                );
            }

            Notification::create(
                fmt::format(
                    "Assisted Click: {} ticks",
                    currentWindow
                ),
                NotificationIcon::None,
                1.0f
            )->show();
        }

        return CCKeyboardDispatcher::dispatchKeyboardMSG(
            key, down, repeat, timestamp
        );
    }
};

class $modify(ACPlayLayer, PlayLayer) {
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();

        ac::g_layer = this;

        if (ac::g_ghost) {
            ac::g_ghost->release();
            ac::g_ghost = nullptr;
        }

        ac::g_ghost = PlayerObject::create(
            1, 0, this, this, true
        );

        if (ac::g_ghost) {
            ac::g_ghost->retain();
            ac::g_ghost->setVisible(false);

            if constexpr (requires {
                ac::g_ghost->m_playEffects = false;
            }) {
                ac::g_ghost->m_playEffects = false;
            }

            ac_console::line(
                "[Assisted Click] Ghost simulator created"
            );
        }
        else {
            log::error("[AC] Ghost simulator creation failed");
        }
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        ac::clearState();
    }

    void onExit() {
        ac::clearState();

        auto* ghost = ac::g_ghost;
        ac::g_ghost = nullptr;
        ac::g_layer = nullptr;

        if (ghost) {
            ghost->release();
        }

        PlayLayer::onExit();
    }

    destroyPlayer(PlayerObject* player, GameObject* object) {
        if (ac::g_simulating && player == ac::g_ghost) {
            ac::g_cloneDied = true;
            return;
        }

        PlayLayer::destroyPlayer(player, object);
    }
};

class $modify(ACBaseLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool player2) {
        if (ac::g_applyingQueuedInput || ac::g_simulating) {
            GJBaseGameLayer::handleButton(
                down, button, player2
            );
            return;
        }

        auto* layer = PlayLayer::get();
        if (!layer) {
            GJBaseGameLayer::handleButton(
                down, button, player2
            );
            return;
        }

        auto* player = player2 ? layer->m_player2 : layer->m_player1;

        if (!player || ac::windowTicks() == 0) {
            if (down) {
                ac::g_pressDelay[
                    ac::buttonKey(button, player2)
                ] = 0;
            }
            else {
                ac::g_pressDelay.erase(
                    ac::buttonKey(button, player2)
                );
            }

            GJBaseGameLayer::handleButton(
                down, button, player2
            );
            return;
        }

        int delay = ac::predictDelay(
            layer,
            player,
            down,
            button,
            player2
        );

        const int key = ac::buttonKey(button, player2);

        if (down) {
            ac::g_pressDelay[key] = delay;
        }
        else {
            auto it = ac::g_pressDelay.find(key);

            if (it != ac::g_pressDelay.end()) {
                delay = std::max(delay, it->second);
                ac::g_pressDelay.erase(it);
            }
        }

        ac::queueInput(
            layer, down, button, player2, delay
        );
    }
};

class $modify(ACPlayerObject, PlayerObject) {
    void update(float dt) {
        auto* layer = PlayLayer::get();

        const bool isRealPlayer =
            layer &&
            this == layer->m_player1 &&
            !ac::g_simulating;

        if (isRealPlayer) {
            ac::dispatchPendingInputs(layer, ac::g_tick + 1);
        }

        const float beforeX = getPositionX();

        PlayerObject::update(dt);

        if (isRealPlayer && dt > 0.0f) {
            ac::g_lastDx = getPositionX() - beforeX;
            ++ac::g_tick;

            ac_console::tickLine(
                "[Assisted Click] TICK " +
                std::to_string(ac::g_tick) +
                " | dt=" + std::to_string(dt) +
                " | x=" + std::to_string(getPositionX()) +
                " | dx=" + std::to_string(ac::g_lastDx)
            );
        }
    }
};

class $modify(ACEffectObject, EffectGameObject) {
    void triggerObject(
        GJBaseGameLayer* layer,
        int id,
        gd::vector<int> const* groups
    ) {
        if (ac::g_simulating) {
            ac::g_triggerEncountered = true;
            return;
        }

        EffectGameObject::triggerObject(
            layer, id, groups
        );
    }
};
```[cite: 1]
