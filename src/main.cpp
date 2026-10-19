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
        bool player1;
    };

    std::deque<PendingInput> g_queue;
    std::unordered_map<int, int> g_pressDelay;

    int windowTicks() {
        return kWindows[g_windowIndex];
    }

    int buttonKey(int button, bool player1) {
        return button + (player1 ? 0 : 10000);
    }

    // Copy only fields that are available in the active bindings.
    // This does not constitute a complete PlayerObject snapshot.
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

    // Simulate one candidate delay from the current pre-input state.
    // Effect triggers invalidate the prediction instead of being activated.
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

                // Some detached PlayerObjects do not advance horizontally.
                // Fall back to the last measured player displacement.
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

        // ActivationGuard restores shared object activation flags here.
        return survived;
    }

    int predictDelay(
        PlayLayer* layer,
        PlayerObject* player,
        bool down,
        int button,
        bool player1
    ) {
        const int limit = windowTicks();

        if (!layer || !player || !g_ghost ||
            !player1 || limit <= 0 || g_tick < 2) {
            return 0;
        }

        const char* action = down ? "DOWN" : "UP";

        // Primero comprobar si el tiempo original ya parece seguro.
        const bool originalSafe =
            simulateDelay(layer, player, down, button, 0);

        log::info(
            "[AC] PRED tick={} input={} button={} window={} "
            "candidate=0 result={} deathFlag={} triggerFlag={}",
            g_tick,
            action,
            button,
            limit,
            originalSafe ? "SAFE" : "UNSAFE",
            g_cloneDied,
            g_triggerEncountered
        );

        // Si el tiempo original parece seguro, no añadir retraso.
        if (originalSafe) {
            return 0;
        }

        // Probar retrasos desde el más corto hasta el límite configurado.
        for (int delay = 1; delay <= limit; ++delay) {
            const bool safe = simulateDelay(
                layer,
                player,
                down,
                button,
                delay
            );

            log::info(
                "[AC] PRED tick={} input={} button={} "
                "candidate={} result={} deathFlag={} triggerFlag={}",
                g_tick,
                action,
                button,
                delay,
                safe ? "SAFE" : "UNSAFE",
                g_cloneDied,
                g_triggerEncountered
            );

            if (safe) {
                log::info(
                    "[AC] DECISION tick={} input={} delay={} ticks",
                    g_tick,
                    action,
                    delay
                );
                return delay;
            }
        }

        log::warn(
            "[AC] DECISION tick={} input={} "
            "no safe delay found; keeping original timing",
            g_tick,
            action
        );

        return 0;
    }

    void queueInput(
        PlayLayer* layer,
        bool down,
        int button,
        bool player1,
        int delay
    ) {
        if (!layer) {
            return;
        }

        if (delay == 0 && g_queue.empty()) {
            g_applyingQueuedInput = true;
            layer->handleButton(down, button, player1);
            g_applyingQueuedInput = false;
            return;
        }

        int dueTick = g_tick + delay;

        if (!g_queue.empty()) {
            dueTick = std::max(
                dueTick,
                g_queue.back().tick
            );

            // Do not let a release collapse onto its queued press.
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
            player1
        });
    }

    void dispatchPendingInputs(PlayLayer* layer, int nextTick) {
        while (!g_queue.empty() &&
               g_queue.front().tick <= nextTick) {
            auto input = g_queue.front();
            g_queue.pop_front();

            log::info(
                "[AC] APPLY tick={} scheduled={} input={} button={} lateBy={}",
                nextTick,
                input.tick,
                input.down ? "DOWN" : "UP",
                input.button,
                std::max(0, nextTick - input.tick)
            );

            g_applyingQueuedInput = true;
            layer->handleButton(
                input.down,
                input.button,
                input.player1
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
}

class $modify(ACKeyboard, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(
        enumKeyCodes key,
        bool down,
        bool repeat,
        double timestamp
    ) {
        if (key == enumKeyCodes::KEY_F6 && down && !repeat) {
            ac::g_windowIndex =
                (ac::g_windowIndex + 1) % 5;

            const int ticks = ac::windowTicks();

            log::info("[AC] Prediction window: {} ticks", ticks);

            Notification::create(
                fmt::format(
                    "Assisted Click: {} ticks",
                    ticks
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

        // Persistent clone: do not create one on every click.
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

            log::info("[AC] Ghost simulator created");
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
        PlayLayer::onExit();

        ac::clearState();

        if (ac::g_ghost) {
            ac::g_ghost->release();
            ac::g_ghost = nullptr;
        }

        ac::g_layer = nullptr;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (ac::g_simulating && player == ac::g_ghost) {
            ac::g_cloneDied = true;
            return;
        }

        PlayLayer::destroyPlayer(player, object);
    }
};

class $modify(ACBaseLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool player1) {
        if (ac::g_applyingQueuedInput || ac::g_simulating) {
            GJBaseGameLayer::handleButton(
                down, button, player1
            );
            return;
        }

        auto* layer = PlayLayer::get();

        if (!layer || !layer->m_player1 ||
            ac::windowTicks() == 0) {
            if (down) {
                ac::g_pressDelay[
                    ac::buttonKey(button, player1)
                ] = 0;
            }
            else {
                ac::g_pressDelay.erase(
                    ac::buttonKey(button, player1)
                );
            }

            GJBaseGameLayer::handleButton(
                down, button, player1
            );
            return;
        }

        int delay = ac::predictDelay(
            layer,
            layer->m_player1,
            down,
            button,
            player1
        );

        const int key = ac::buttonKey(button, player1);

        if (down) {
            ac::g_pressDelay[key] = delay;
        }
        else {
            auto it = ac::g_pressDelay.find(key);

            if (it != ac::g_pressDelay.end()) {
                // Do not shorten the press/release interval.
                delay = std::max(delay, it->second);
                ac::g_pressDelay.erase(it);
            }
        }

        ac::queueInput(
            layer, down, button, player1, delay
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
            // Dispatch scheduled inputs before the next physics update.
            ac::dispatchPendingInputs(layer, ac::g_tick + 1);
        }

        const float beforeX = getPositionX();

        PlayerObject::update(dt);

        if (isRealPlayer && dt > 0.0f) {
            ac::g_lastDx = getPositionX() - beforeX;
            ++ac::g_tick;
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
            // Do not let predicted paths change real level state.
            ac::g_triggerEncountered = true;
            return;
        }

        EffectGameObject::triggerObject(
            layer, id, groups
        );
    }
};
