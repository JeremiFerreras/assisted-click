// InputBuffer (Assisted Click) - src/main.cpp  -- reescritura v5.0
//
// Idea: cuando se recibe un clic (PRESS/RELEASE) se simula con un "fantasma" del jugador
// si el timing original muere. Si muere, se busca el menor retraso (en ticks de 1/240 s)
// dentro del limite de la etapa activa con el que el fantasma sobreviva, y la entrada se
// aplica retrasada. Uso personal / offline: NO sirve para enviar records a listas.
//
// Consola: SOLO banner, ciclo F6 y lineas "SAVED". Todo lo demas va al log de Geode.

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

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

// ============================================================================
// Consola (solo Windows)
// ============================================================================
namespace ac_console {

#ifdef _WIN32

static std::mutex g_mutex;
static HANDLE g_output = INVALID_HANDLE_VALUE;
static bool g_ready = false;
static bool g_bannerShown = false;

static bool init() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_ready) return true;

    FreeConsole();
    if (!AllocConsole()) return false;

    g_output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (g_output == nullptr || g_output == INVALID_HANDLE_VALUE) {
        FreeConsole();
        return false;
    }

    SetConsoleTitleA("InputBuffer - Live Console");
    SetConsoleOutputCP(CP_UTF8);
    g_ready = true;
    return true;
}

static void line(std::string const& message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_ready || g_output == INVALID_HANDLE_VALUE) return;

    std::string out = message + "\r\n";
    DWORD written = 0;
    WriteConsoleA(g_output, out.data(), static_cast<DWORD>(out.size()), &written, nullptr);
}

#else

static bool init() { return false; }
static void line(std::string const&) {}

#endif

} // namespace ac_console

// ============================================================================
// Nucleo
// ============================================================================
namespace ac {

    // ---- Etapas: {ticks para todo, ticks para wave}. 1 tick = 0.25 frames @ 60fps
    struct Stage { int all; int wave; };
    constexpr Stage kStages[] = { {2, 2}, {4, 4}, {8, 8}, {16, 8}, {0, 0} };
    constexpr int kStageCount = 5;

    constexpr int kHorizon = 64;      // ticks simulados tras aplicar la entrada
    constexpr int kMaxSimSteps = 160; // tope de seguridad por simulacion
    constexpr bool kLogVerdicts = true; // veredicto por clic -> log de Geode (NO consola)

    int g_stage = 0; // empieza en STAGE 1

    std::string stageText(int i) {
        auto const& s = kStages[i];
        if (s.all == 0) {
            return fmt::format("STAGE {}: OFF (100% Raw Input)", i + 1);
        }
        if (s.all == s.wave) {
            return fmt::format(
                "STAGE {}: {} Ticks All | {} Ticks Wave ({:.1f}f @ 60fps)",
                i + 1, s.all, s.wave, s.all * 0.25
            );
        }
        return fmt::format(
            "STAGE {}: {} Ticks All | {} Ticks Wave ({:.1f}f / {:.1f}f @ 60fps)",
            i + 1, s.all, s.wave, s.all * 0.25, s.wave * 0.25
        );
    }

    void showBannerOnce() {
        static bool shown = false;
        if (shown || !ac_console::init()) return;
        shown = true;

        auto const& s = kStages[0];
        ac_console::line("===================================================");
        ac_console::line("[InputBuffer] v5.0 LOADED (Ghost Trajectory Rewrite)");
        ac_console::line(fmt::format(
            "[InputBuffer] Starting Mode: STAGE 1 ({} All / {} Wave)", s.all, s.wave
        ));
        ac_console::line("[InputBuffer] Press F6 in-level to cycle: 2/2 -> 4/4 -> 8/8 -> 16/8 -> OFF");
        ac_console::line("===================================================");
    }

    // Limite activo segun el modo actual del jugador.
    int limitFor(PlayerObject* p) {
        auto const& s = kStages[g_stage];
        return (p && p->m_isDart) ? s.wave : s.all;
    }

    // ---- Estado de la partida
    int g_tick = 0;           // updates reales del jugador 1 con dt > 0
    float g_stepDt = 0.25f;   // ultimo dt real
    float g_lastDx = 0.0f;
    int g_dtStable = 0;

    PlayLayer* g_layer = nullptr;
    PlayerObject* g_ghost = nullptr;
    PlayLayer* g_ghostOwner = nullptr;

    bool g_simulating = false;
    bool g_cloneDied = false;
    bool g_triggerEncountered = false;
    bool g_applyingQueuedInput = false;

    // ---- Diagnostico (solo log de Geode)
    const char* g_unknownReason = "";
    int g_clicksSeen = 0, g_clicksPredicted = 0;
    int g_statKept = 0, g_statSaved = 0, g_statNoSolution = 0, g_statUnknown = 0;
    bool g_warnedX = false, g_warnedSeed = false, g_warnedWorld = false;

    enum class SimResult { Safe, Died, Unknown };

    struct SimOut {
        SimResult result;
        int steps; // pasos simulados hasta morir / hasta terminar
    };

    // ---- Cola de entradas
    struct PendingInput {
        int tick; // tick absoluto de aplicacion
        bool down;
        int button;
        bool isPlayer1;
    };

    struct KeyState {
        int lastDue = 0;
        int pending = 0;
    };

    std::deque<PendingInput> g_queue;
    std::unordered_map<int, KeyState> g_keys;

    int buttonKey(int button, bool isPlayer1) {
        return button + (isPlayer1 ? 0 : 10000);
    }

    struct SimulationScope {
        bool prev;
        SimulationScope() : prev(g_simulating) {
            g_simulating = true;
            g_cloneDied = false;
            g_triggerEncountered = false;
        }
        ~SimulationScope() { g_simulating = prev; }
    };

    struct QueuedInputScope {
        bool prev;
        QueuedInputScope() : prev(g_applyingQueuedInput) { g_applyingQueuedInput = true; }
        ~QueuedInputScope() { g_applyingQueuedInput = prev; }
    };

    void clearInputs() {
        g_queue.clear();
        g_keys.clear();
    }

    void clearState() {
        clearInputs();
        g_tick = 0;
        g_stepDt = 0.25f;
        g_lastDx = 0.0f;
        g_dtStable = 0;
        g_cloneDied = false;
        g_triggerEncountered = false;
        g_applyingQueuedInput = false;
    }

    void clearStats() {
        g_unknownReason = "";
        g_clicksSeen = g_clicksPredicted = 0;
        g_statKept = g_statSaved = g_statNoSolution = g_statUnknown = 0;
        g_warnedX = g_warnedSeed = g_warnedWorld = false;
    }

    // ========================================================================
    // Fantasma
    // ========================================================================

    // Equivalente a PlayerObject::resetCollisionLog (inlineado en Windows), igual que
    // hace Click-Between-Frames. Necesario para la wave.
    template <class P>
    void clearCollisionLog(P* p) {
        if constexpr (requires { p->m_collisionLogTop; p->m_lastCollisionLeft; }) {
            p->m_collisionLogTop->removeAllObjects();
            p->m_collisionLogBottom->removeAllObjects();
            p->m_collisionLogLeft->removeAllObjects();
            p->m_collisionLogRight->removeAllObjects();
            p->m_lastCollisionLeft = -1;
            p->m_lastCollisionRight = -1;
            p->m_lastCollisionBottom = -1;
            p->m_lastCollisionTop = -1;
        } else {
            p->resetCollisionLog(false);
        }
    }

    template <class P>
    void seedGhost(P* dst, P* src) {
        if (!dst || !src) return;

        dst->resetStateVariables();
        dst->resetTouchedRings(true);
        dst->resetCollisionLog(true);
        dst->resetCollisionValues();

        dst->setPosition(src->getPosition());
        dst->setRotation(src->getRotation());
        dst->setScale(src->getScale());

        #define AC_COPY(F) \
            if constexpr (requires { dst->F = src->F; }) { dst->F = src->F; }

        // posicion / velocidad / fisica
        AC_COPY(m_position) AC_COPY(m_lastPosition) AC_COPY(m_yVelocity) AC_COPY(m_fallSpeed)
        AC_COPY(m_playerSpeed) AC_COPY(m_speedMultiplier) AC_COPY(m_gravity) AC_COPY(m_gravityMod)
        AC_COPY(m_yStart) AC_COPY(m_platformerXVelocity) AC_COPY(m_physDeltaRelated)
        AC_COPY(m_vehicleSize) AC_COPY(m_xVelocityRelated) AC_COPY(m_xVelocityRelated2)
        AC_COPY(m_lastPortalPos) AC_COPY(m_shipRotation) AC_COPY(m_affectedByForces) AC_COPY(m_isLocked)
        // modo
        AC_COPY(m_isShip) AC_COPY(m_isBird) AC_COPY(m_isBall) AC_COPY(m_isDart) AC_COPY(m_isRobot)
        AC_COPY(m_isSpider) AC_COPY(m_isSwing) AC_COPY(m_isUpsideDown) AC_COPY(m_isGoingLeft)
        AC_COPY(m_isSideways) AC_COPY(m_isPlatformer) AC_COPY(m_isSecondPlayer)
        AC_COPY(m_controlsDisabled) AC_COPY(m_isDashing) AC_COPY(m_dashFireFrame)
        // contacto / pendientes
        AC_COPY(m_isOnGround) AC_COPY(m_isOnGround2) AC_COPY(m_isOnGround3) AC_COPY(m_isOnGround4)
        AC_COPY(m_isOnSlope) AC_COPY(m_wasOnSlope) AC_COPY(m_isOnIce) AC_COPY(m_slopeVelocity)
        AC_COPY(m_slopeRotation) AC_COPY(m_currentSlopeYVelocity) AC_COPY(m_isCurrentSlopeTop)
        AC_COPY(m_maybeUpsideDownSlope) AC_COPY(m_isAccelerating) AC_COPY(m_collidedTopMinY)
        AC_COPY(m_collidedBottomMaxY) AC_COPY(m_collidedLeftMaxX) AC_COPY(m_collidedRightMinX)
        AC_COPY(m_lastGroundedPos)
        // estados state*
        AC_COPY(m_stateOnGround) AC_COPY(m_stateNoStickX) AC_COPY(m_stateNoStickY)
        AC_COPY(m_stateBoostX) AC_COPY(m_stateBoostY) AC_COPY(m_stateScale) AC_COPY(m_stateNoAutoJump)
        AC_COPY(m_stateDartSlide) AC_COPY(m_stateHitHead) AC_COPY(m_stateFlipGravity)
        AC_COPY(m_stateForce) AC_COPY(m_stateForceVector) AC_COPY(m_stateRingJump)
        AC_COPY(m_stateRingJump2) AC_COPY(m_stateJumpBuffered)
        // botones
        AC_COPY(m_holdingButtons) AC_COPY(m_holdingRight) AC_COPY(m_holdingLeft)
        AC_COPY(m_jumpBuffered) AC_COPY(m_wasJumpBuffered) AC_COPY(m_wasRobotJump)
        // anillos / pads / portales
        AC_COPY(m_touchedRings) AC_COPY(m_touchedRing) AC_COPY(m_touchedCustomRing)
        AC_COPY(m_touchedPad) AC_COPY(m_touchedGravityPortal) AC_COPY(m_padRingRelated)
        AC_COPY(m_ringJumpRelated) AC_COPY(m_hasEverHitRing) AC_COPY(m_isBeingSpawnedByDualPortal)
        // tiempos
        AC_COPY(m_lastLandTime) AC_COPY(m_lastJumpTime) AC_COPY(m_lastFlipTime)
        AC_COPY(m_lastSpiderFlipTime) AC_COPY(m_totalTime) AC_COPY(m_gameModeChangedTime)
        AC_COPY(m_hasEverJumped)

        #undef AC_COPY

        dst->m_isDead = false;
        dst->m_playEffects = false;
    }

    // El fantasma vive ligado a su PlayLayer. NO se destruye en onExit (cocos lo llama
    // tambien al pausar); se libera en PlayLayer::onQuit.
    bool ensureGhost(PlayLayer* layer) {
        if (!layer) return false;
        if (g_ghost && g_ghostOwner == layer) return true;

        // Fantasma de otra capa (posiblemente ya destruida): se suelta sin release.
        g_ghost = nullptr;
        g_ghostOwner = nullptr;

        auto* ghost = PlayerObject::create(1, 0, layer, layer, true);
        if (!ghost) {
            log::error("[IB] ghost creation failed");
            return false;
        }

        ghost->retain();
        ghost->setVisible(false);
        ghost->m_playEffects = false;

        g_ghost = ghost;
        g_ghostOwner = layer;
        log::info("[IB] ghost created");
        return true;
    }

    // ========================================================================
    // Proteccion del mundo real (flags de activacion de objetos)
    // ========================================================================
    struct ActivationBits {
        bool isActivated = false;
        bool hasEnhanced = false;
        bool p1 = false, p2 = false;
    };

    struct ActivationGuard {
        std::vector<std::pair<GameObject*, ActivationBits>> saved;

        explicit ActivationGuard(PlayLayer* layer) {
            if (!layer || !layer->m_objects) return;
            saved.reserve(static_cast<size_t>(layer->m_objects->count()));
            for (auto* obj : CCArrayExt<GameObject*>(layer->m_objects)) {
                if (!obj) continue;
                ActivationBits bits;
                bits.isActivated = obj->m_isActivated;
                if (auto* e = typeinfo_cast<EnhancedGameObject*>(obj)) {
                    bits.hasEnhanced = true;
                    bits.p1 = e->m_activatedByPlayer1;
                    bits.p2 = e->m_activatedByPlayer2;
                }
                saved.emplace_back(obj, bits);
            }
        }

        void restore() {
            for (auto& [obj, bits] : saved) {
                obj->m_isActivated = bits.isActivated;
                if (bits.hasEnhanced) {
                    if (auto* e = typeinfo_cast<EnhancedGameObject*>(obj)) {
                        e->m_activatedByPlayer1 = bits.p1;
                        e->m_activatedByPlayer2 = bits.p2;
                    }
                }
            }
        }

        ~ActivationGuard() { restore(); }
    };

    // ========================================================================
    // Calculo del tick de aplicacion
    // ========================================================================
    // delay 0 = proximo update del jugador; delay N = N updates despues.
    // Mismo boton: siempre al menos 1 tick despues de su ultima entrada pendiente.
    int effectiveDue(int key, int delay) {
        int due = g_tick + 1 + delay;
        auto it = g_keys.find(key);
        if (it != g_keys.end() && it->second.pending > 0) {
            due = std::max(due, it->second.lastDue + 1);
        }
        return due;
    }

    // ========================================================================
    // Simulacion
    // ========================================================================
    struct SimEvent { int step; bool down; int button; };

    static SimOut unknown(const char* why) {
        g_unknownReason = why;
        return {SimResult::Unknown, 0};
    }

    SimOut simulate(PlayLayer* layer, PlayerObject* real, bool down, int button, int candidateDue) {
        if (!layer || !real || !g_ghost) return unknown("no-ghost");
        if (g_dtStable < 2) return unknown("dt-unstable");

        const int base = g_tick + 1;

        // Eventos: cola real pendiente + candidato.
        std::vector<SimEvent> events;
        events.reserve(g_queue.size() + 1);
        for (auto const& q : g_queue) {
            events.push_back({std::max(0, q.tick - base), q.down, q.button});
        }
        events.push_back({std::max(0, candidateDue - base), down, button});
        std::stable_sort(events.begin(), events.end(),
            [](SimEvent const& a, SimEvent const& b) { return a.step < b.step; });

        const int totalSteps = events.back().step + kHorizon;
        if (totalSteps > kMaxSimSteps) return unknown("too-many-steps");

        const float dt = g_stepDt > 0.0f ? g_stepDt : 0.25f;

        const float rx = real->getPositionX();
        const float ry = real->getPositionY();
        const double rv = real->m_yVelocity;

        SimOut out{SimResult::Safe, totalSteps};

        {
            SimulationScope scope;
            seedGhost(g_ghost, real);

            // Comprobacion unica de que el clon arranca igual que el real.
            if (!g_warnedSeed) {
                g_warnedSeed = true;
                const bool ok =
                    std::abs(g_ghost->getPositionX() - rx) < 0.001f &&
                    std::abs(g_ghost->getPositionY() - ry) < 0.001f &&
                    std::abs(g_ghost->m_yVelocity - rv) < 0.001 &&
                    g_ghost->m_isDart == real->m_isDart &&
                    g_ghost->m_isShip == real->m_isShip &&
                    g_ghost->m_isBall == real->m_isBall;
                log::info("[IB] seed check: {}", ok ? "OK" : "MISMATCH");
            }

            size_t next = 0;
            for (int step = 0; step < totalSteps; ++step) {
                while (next < events.size() && events[next].step <= step) {
                    auto pb = static_cast<PlayerButton>(events[next].button);
                    if (events[next].down) g_ghost->pushButton(pb);
                    else g_ghost->releaseButton(pb);
                    ++next;
                }

                const float bx = g_ghost->getPositionX();
                g_ghost->update(dt);

                if (step == 0 && std::abs(g_ghost->getPositionX() - bx) < 0.0001f &&
                    std::abs(g_lastDx) > 0.0001f) {
                    if (!g_warnedX) {
                        g_warnedX = true;
                        log::warn("[IB] ghost X does not advance while real player does");
                    }
                    out = unknown("ghost-x-static");
                    break;
                }

                // Mismo criterio que Click-Between-Frames: dt = 0 salvo en pendiente
                // (y la wave siempre 0). ignoreDamage = false para que el clon pueda morir.
                const bool slope = g_ghost->m_isOnSlope && !g_ghost->m_isDart;
                layer->checkCollisions(g_ghost, slope ? dt : 0.0f, false);
                g_ghost->updateRotation(dt);
                clearCollisionLog(g_ghost);

                if (g_triggerEncountered) {
                    out = unknown("trigger");
                    break;
                }
                if (g_cloneDied || g_ghost->m_isDead) {
                    out = {SimResult::Died, step + 1};
                    break;
                }
            }
        }

        // El mundo real no debe haber cambiado.
        if (std::abs(real->getPositionX() - rx) > 0.0001f ||
            std::abs(real->getPositionY() - ry) > 0.0001f ||
            std::abs(real->m_yVelocity - rv) > 0.0001) {
            if (!g_warnedWorld) {
                g_warnedWorld = true;
                log::warn("[IB] simulation modified the real player state");
            }
            return unknown("real-state-changed");
        }

        return out;
    }

    int predictDelay(PlayLayer* layer, PlayerObject* player, bool down, int button) {
        const int limit = limitFor(player);
        if (!layer || !player || limit <= 0 || g_tick < 2 || player->m_isDead) return 0;
        if (!ensureGhost(layer)) return 0;

        ++g_clicksPredicted;
        g_unknownReason = "";

        const int key = buttonKey(button, true);
        ActivationGuard guard(layer);

        int prevDue = effectiveDue(key, 0);
        const SimOut base = simulate(layer, player, down, button, prevDue);
        guard.restore();

        int chosen = 0;

        if (base.result == SimResult::Safe) {
            ++g_statKept;
            if constexpr (kLogVerdicts) {
                log::debug("[IB] {} tick {} -> SAFE", down ? "PRESS" : "RELEASE", g_tick);
            }
        }
        else if (base.result == SimResult::Unknown) {
            // No inventar alternativas: se conserva la entrada original.
            ++g_statUnknown;
            if constexpr (kLogVerdicts) {
                log::debug("[IB] {} tick {} -> UNKNOWN ({})", down ? "PRESS" : "RELEASE", g_tick, g_unknownReason);
            }
        }
        else {
            bool found = false;
            SimOut best{SimResult::Died, 0};

            for (int d = 1; d <= limit; ++d) {
                const int due = effectiveDue(key, d);
                if (due == prevDue) continue;
                prevDue = due;

                const SimOut r = simulate(layer, player, down, button, due);
                guard.restore();

                if (r.result == SimResult::Safe) {
                    chosen = d;
                    best = r;
                    found = true;
                    break;
                }
            }

            if (found) {
                ++g_statSaved;
                ac_console::line(fmt::format(
                    "[InputBuffer] *** SAVED +{} TICKS ({:.2f} frames @ 60fps) {}! "
                    "(Frame0: {} -> Delayed: {}) [Limit: {}] ***",
                    chosen, chosen * 0.25, down ? "PRESS" : "RELEASE",
                    base.steps, best.steps, limit
                ));
            } else {
                ++g_statNoSolution;
                if constexpr (kLogVerdicts) {
                    log::debug("[IB] {} tick {} -> DEATH, no safe delay in {}", down ? "PRESS" : "RELEASE", g_tick, limit);
                }
            }
        }

        return chosen;
    }

    // ========================================================================
    // Cola
    // ========================================================================
    void queueInput(PlayLayer* layer, bool down, int button, bool isPlayer1, int delay) {
        if (!layer) return;

        const int key = buttonKey(button, isPlayer1);
        auto it = g_keys.find(key);
        const bool keyPending = it != g_keys.end() && it->second.pending > 0;

        // Camino directo: sin retraso, cola vacia y nada pendiente de este boton.
        if (delay == 0 && g_queue.empty() && !keyPending) {
            QueuedInputScope scope;
            layer->handleButton(down, button, isPlayer1);
            return;
        }

        const int due = effectiveDue(key, delay);

        auto pos = std::upper_bound(
            g_queue.begin(), g_queue.end(), due,
            [](int v, PendingInput const& item) { return v < item.tick; });
        g_queue.insert(pos, PendingInput{due, down, button, isPlayer1});

        auto& ks = g_keys[key];
        ks.lastDue = std::max(ks.lastDue, due);
        ++ks.pending;
    }

    void dispatchPending(PlayLayer* layer, int nextTick) {
        if (!layer) return;

        while (!g_queue.empty() && g_queue.front().tick <= nextTick) {
            const auto input = g_queue.front();
            g_queue.pop_front();

            auto it = g_keys.find(buttonKey(input.button, input.isPlayer1));
            if (it != g_keys.end() && --it->second.pending <= 0) {
                g_keys.erase(it);
            }

            QueuedInputScope scope;
            layer->handleButton(input.down, input.button, input.isPlayer1);
        }
    }

} // namespace ac

$on_mod(Loaded) {
    ac::showBannerOnce();
}

// ============================================================================
// Hooks
// ============================================================================
class $modify(IBKeyboard, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(cocos2d::enumKeyCodes key, bool down, bool repeat, double timestamp) {
        if (down && !repeat) {
            if (key == cocos2d::enumKeyCodes::KEY_F6) {
                ac::g_stage = (ac::g_stage + 1) % ac::kStageCount;
                ac_console::line("[InputBuffer] F6 Cycle -> " + ac::stageText(ac::g_stage));
            }
            else if (key == cocos2d::enumKeyCodes::KEY_F7) {
                // Volcado de estado SOLO al log de Geode.
                log::info(
                    "[IB] STATUS stage={} ghost={} tick={} dtStable={} clicks={} predicted={} "
                    "kept={} saved={} nosol={} unknown={} lastUnknown={}",
                    ac::g_stage + 1, ac::g_ghost ? "ok" : "NULL", ac::g_tick, ac::g_dtStable,
                    ac::g_clicksSeen, ac::g_clicksPredicted, ac::g_statKept, ac::g_statSaved,
                    ac::g_statNoSolution, ac::g_statUnknown, ac::g_unknownReason
                );
            }
        }

        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
    }
};

class $modify(IBPlayLayer, PlayLayer) {
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();

        ac::showBannerOnce(); // reintento si la consola fallo en Loaded
        ac::g_layer = this;
        ac::clearState();
        ac::clearStats();

        // Fantasma de un nivel anterior: su capa ya no existe, se suelta sin release.
        ac::g_ghost = nullptr;
        ac::g_ghostOwner = nullptr;
        ac::ensureGhost(this);
    }

    void resetLevel() {
        ac::clearState();
        PlayLayer::resetLevel();
        ac::clearState();
    }

    // onExit tambien se dispara al pausar: no se destruye nada.
    void onExit() {
        ac::clearInputs();
        PlayLayer::onExit();
    }

    // Salida real del nivel.
    void onQuit() {
        ac::clearState();

        auto* ghost = ac::g_ghost;
        ac::g_ghost = nullptr;
        ac::g_ghostOwner = nullptr;
        ac::g_layer = nullptr;
        if (ghost) ghost->release();

        PlayLayer::onQuit();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (ac::g_simulating && player == ac::g_ghost) {
            ac::g_cloneDied = true;
            return;
        }
        PlayLayer::destroyPlayer(player, object);
    }
};

class $modify(IBBaseLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (ac::g_applyingQueuedInput || ac::g_simulating) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        auto* layer = PlayLayer::get();

        // Solo si ESTA capa es el PlayLayer activo, y solo jugador 1.
        if (!layer ||
            static_cast<GJBaseGameLayer*>(layer) != static_cast<GJBaseGameLayer*>(this) ||
            !isPlayer1) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        ++ac::g_clicksSeen;

        int delay = 0;
        auto* player = layer->m_player1;
        if (player && ac::kStages[ac::g_stage].all > 0) {
            delay = ac::predictDelay(layer, player, down, button);
        }

        ac::queueInput(layer, down, button, isPlayer1, delay);
    }
};

class $modify(IBPlayerObject, PlayerObject) {
    void update(float dt) {
        auto* layer = PlayLayer::get();
        const bool real = layer && this == layer->m_player1 && !ac::g_simulating;

        if (real) {
            ac::dispatchPending(layer, ac::g_tick + 1);
        }

        const float beforeX = getPositionX();
        PlayerObject::update(dt);

        if (real && dt > 0.0f) {
            if (std::abs(dt - ac::g_stepDt) < 0.00002f) ++ac::g_dtStable;
            else ac::g_dtStable = 0;

            ac::g_stepDt = dt;
            ac::g_lastDx = getPositionX() - beforeX;
            ++ac::g_tick;
        }
    }
};

class $modify(IBEffectObject, EffectGameObject) {
    void triggerObject(GJBaseGameLayer* layer, int id, gd::vector<int> const* groups) {
        if (ac::g_simulating) {
            ac::g_triggerEncountered = true; // estado no modelado -> Unknown
            return;
        }
        EffectGameObject::triggerObject(layer, id, groups);
    }
};
