// Assisted Click - src/main.cpp
// Revisado contra bindings Geode 2.2081 (GeometryDash.bro) y la lista de bugs
// "Aplicar_CON_PRECISION". Ver comentarios [FIX x.y] para cada correccion.

// [FIX P0/P1] NOMINMAX y windows.h SOLO en Windows y ANTES de los headers de Geode.
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
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace geode::prelude;

// ============================================================================
// Consola
// ============================================================================
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

    if (message.size() > 150) {
        message.resize(150);
    }

    if (message.size() < g_tickLineLength) {
        message.append(g_tickLineLength - message.size(), ' ');
    }

    writeRaw("\r");
    writeRaw(message);

    g_tickLineLength = message.size();
    g_tickLineActive = true;
}

// [FIX 5] Idempotente: si init() fallo en Loaded, se puede reintentar
// llamando de nuevo (se reintenta al crear cada PlayLayer).
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
    line("[Assisted Click] v1.0.0 LOADED (Community Edition)");
    line("[Assisted Click] Events: DECISION / SCHEDULED / APPLIED");
    line("[Assisted Click] Tick status updates in place (every 4 ticks)");
    line("[Assisted Click] Press F6 in-level to cycle prediction window");
    line("[Assisted Click] Press F7 for a status dump (also goes to Geode log)");
    line("===================================================");
}

#else

inline bool init() { return false; }
inline void line(const std::string&) {}
inline void tickLine(std::string) {}
inline void showBannerOnce() {}

#endif

} // namespace ac_console

$on_mod(Loaded) {
    ac_console::showBannerOnce();
}

// ============================================================================
// Logica del mod
// ============================================================================
namespace ac {

    float g_stepDt = 0.25f;
    constexpr int kLookAhead = 32;
    constexpr int kMaxSimSteps = 256;

    // [FIX 3.2] Si es true se reactiva la compensacion antigua de X (NO recomendado).
    // Por defecto false: si el clon no avanza en X la simulacion es "Unknown".
    constexpr bool kForceXCompensation = false;

    const int kWindows[] = {0, 2, 4, 8, 16};
    int g_windowIndex = 0;

    int g_tick = 0;
    float g_lastDx = 0.0f;
    int g_dtStable = 0; // [FIX 4.3] ticks consecutivos con el mismo dt

    PlayLayer* g_layer = nullptr;
    PlayerObject* g_ghost = nullptr;
    PlayLayer* g_ghostOwner = nullptr; // capa a la que pertenece el fantasma

    bool g_simulating = false;
    bool g_cloneDied = false;
    bool g_triggerEncountered = false;
    bool g_applyingQueuedInput = false;

    // [DIAG] Una linea por cada clic con el veredicto (SAFE / UNKNOWN(motivo) / DEATH).
    // Poner en false cuando todo funcione.
    constexpr bool kVerboseClicks = true;
    const char* g_unknownReason = "";
    bool g_loggedFirstUpdate = false;
    bool g_loggedFirstButton = false;
    int g_clicksSeen = 0;
    int g_clicksPredicted = 0;

    // Diagnostico (por nivel)
    bool g_seedVerified = false;
    bool g_warnedX = false;
    bool g_warnedDt = false;
    bool g_warnedWorld = false;
    int g_statKept = 0;        // original predicho seguro
    int g_statChanged = 0;     // retraso aplicado
    int g_statNoSolution = 0;  // moriria y no hay retraso seguro
    int g_statUnknown = 0;     // simulacion no concluyente -> original
    long long g_lastSimUs = 0;
    long long g_maxSimUs = 0;

    // [FIX 3.4] Resultado de simulacion con tres estados.
    enum class SimResult {
        Safe,
        Died,
        Unknown
    };

    struct PendingInput {
        int tick;      // tick absoluto de aplicacion
        bool down;
        int button;
        bool isPlayer1;
    };

    // [FIX 2.1] Estado por boton: ultimo tick programado y cuantas entradas siguen pendientes.
    struct KeyState {
        int lastDue = 0;
        int pending = 0;
    };

    std::deque<PendingInput> g_queue;
    std::unordered_map<int, KeyState> g_keyState;

    int windowTicks() {
        return kWindows[g_windowIndex];
    }

    int buttonKey(int button, bool isPlayer1) {
        return button + (isPlayer1 ? 0 : 10000);
    }

    // [FIX 4.5] Guards con restauracion del valor previo (reentrada segura).
    struct SimulationScope {
        bool prev;

        SimulationScope() : prev(g_simulating) {
            g_simulating = true;
            g_cloneDied = false;
            g_triggerEncountered = false;
        }

        ~SimulationScope() {
            g_simulating = prev;
        }
    };

    struct QueuedInputScope {
        bool prev;

        QueuedInputScope() : prev(g_applyingQueuedInput) {
            g_applyingQueuedInput = true;
        }

        ~QueuedInputScope() {
            g_applyingQueuedInput = prev;
        }
    };

    // [FIX 3.3] Inicializacion unica del clon. Cada AC_COPY solo compila si el
    // miembro existe en los bindings (requires dentro de plantilla).
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
            if constexpr (requires { dst->F = src->F; }) { \
                dst->F = src->F; \
            }

        // Posicion / velocidad / fisica
        AC_COPY(m_position)
        AC_COPY(m_lastPosition)
        AC_COPY(m_yVelocity)
        AC_COPY(m_fallSpeed)
        AC_COPY(m_playerSpeed)
        AC_COPY(m_speedMultiplier)
        AC_COPY(m_gravity)
        AC_COPY(m_gravityMod)
        AC_COPY(m_yStart)
        AC_COPY(m_platformerXVelocity)
        AC_COPY(m_physDeltaRelated)
        AC_COPY(m_vehicleSize)
        AC_COPY(m_xVelocityRelated)
        AC_COPY(m_xVelocityRelated2)
        AC_COPY(m_lastPortalPos)
        AC_COPY(m_shipRotation)
        AC_COPY(m_affectedByForces)
        AC_COPY(m_isLocked)

        // Modo de juego
        AC_COPY(m_isShip)
        AC_COPY(m_isBird)
        AC_COPY(m_isBall)
        AC_COPY(m_isDart)
        AC_COPY(m_isRobot)
        AC_COPY(m_isSpider)
        AC_COPY(m_isSwing)
        AC_COPY(m_isUpsideDown)
        AC_COPY(m_isGoingLeft)
        AC_COPY(m_isSideways)
        AC_COPY(m_isPlatformer)
        AC_COPY(m_isSecondPlayer)
        AC_COPY(m_controlsDisabled)
        AC_COPY(m_isDashing) // [FIX 3.3] faltaba
        AC_COPY(m_dashFireFrame)

        // Contacto / pendientes
        AC_COPY(m_isOnGround)
        AC_COPY(m_isOnGround2)
        AC_COPY(m_isOnGround3)
        AC_COPY(m_isOnGround4)
        AC_COPY(m_isOnSlope)
        AC_COPY(m_wasOnSlope)
        AC_COPY(m_isOnIce)
        AC_COPY(m_slopeVelocity)
        AC_COPY(m_slopeRotation)
        AC_COPY(m_currentSlopeYVelocity)
        AC_COPY(m_isCurrentSlopeTop)
        AC_COPY(m_maybeUpsideDownSlope)
        AC_COPY(m_isAccelerating)
        AC_COPY(m_collidedTopMinY)
        AC_COPY(m_collidedBottomMaxY)
        AC_COPY(m_collidedLeftMaxX)
        AC_COPY(m_collidedRightMinX)
        AC_COPY(m_lastGroundedPos)

        // Estados "state*" internos
        AC_COPY(m_stateOnGround)
        AC_COPY(m_stateNoStickX)
        AC_COPY(m_stateNoStickY)
        AC_COPY(m_stateBoostX)
        AC_COPY(m_stateBoostY)
        AC_COPY(m_stateScale)
        AC_COPY(m_stateNoAutoJump)
        AC_COPY(m_stateDartSlide)
        AC_COPY(m_stateHitHead)
        AC_COPY(m_stateFlipGravity)
        AC_COPY(m_stateForce)
        AC_COPY(m_stateForceVector)
        AC_COPY(m_stateRingJump)
        AC_COPY(m_stateRingJump2)
        AC_COPY(m_stateJumpBuffered)

        // Botones
        AC_COPY(m_holdingButtons)
        AC_COPY(m_holdingRight)
        AC_COPY(m_holdingLeft)
        AC_COPY(m_jumpBuffered)
        AC_COPY(m_wasJumpBuffered)
        AC_COPY(m_wasRobotJump)

        // Anillos / pads / portales (valores, no punteros a objetos del mundo)
        AC_COPY(m_touchedRings)
        AC_COPY(m_touchedRing)
        AC_COPY(m_touchedCustomRing)
        AC_COPY(m_touchedPad)
        AC_COPY(m_touchedGravityPortal)
        AC_COPY(m_padRingRelated)
        AC_COPY(m_ringJumpRelated)
        AC_COPY(m_hasEverHitRing)
        AC_COPY(m_isBeingSpawnedByDualPortal)

        // Tiempos
        AC_COPY(m_lastLandTime)
        AC_COPY(m_lastJumpTime)
        AC_COPY(m_lastFlipTime)
        AC_COPY(m_lastSpiderFlipTime)
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

    // [FIX 3.3] Verificacion de que el clon empieza igual que el jugador real.
    void verifySeedOnce(PlayerObject* real) {
        if (g_seedVerified || !g_ghost || !real) return;
        g_seedVerified = true;

        const bool posOk =
            std::abs(g_ghost->getPositionX() - real->getPositionX()) < 0.001f &&
            std::abs(g_ghost->getPositionY() - real->getPositionY()) < 0.001f;
        const bool velOk = std::abs(g_ghost->m_yVelocity - real->m_yVelocity) < 0.001;
        const bool modeOk =
            g_ghost->m_isShip == real->m_isShip &&
            g_ghost->m_isBall == real->m_isBall &&
            g_ghost->m_isDart == real->m_isDart &&
            g_ghost->m_isUpsideDown == real->m_isUpsideDown;

        if (!posOk || !velOk || !modeOk) {
            ac_console::line(
                "[Assisted Click] WARNING seed mismatch | pos=" +
                std::string(posOk ? "ok" : "BAD") +
                " yVel=" + (velOk ? "ok" : "BAD") +
                " mode=" + (modeOk ? "ok" : "BAD")
            );
        } else {
            ac_console::line("[Assisted Click] Ghost seed verified OK");
        }
    }

    // ------------------------------------------------------------------
    // Proteccion de objetos activados
    // ------------------------------------------------------------------
    struct ActivationBits {
        bool hasIsActivated = false;
        bool isActivated = false;
        bool hasP1 = false;
        bool p1 = false;
        bool hasP2 = false;
        bool p2 = false;
    };

    template <class O>
    ActivationBits captureActivation(O* obj) {
        ActivationBits state;
        if (!obj) return state;

        if constexpr (requires { obj->m_isActivated; }) {
            state.hasIsActivated = true;
            state.isActivated = obj->m_isActivated;
        }

        // [FIX 3.5] m_activatedByPlayer1/2 viven en EnhancedGameObject (bindings 2.2081).
        if (auto* enhanced = typeinfo_cast<EnhancedGameObject*>(obj)) {
            state.hasP1 = true;
            state.p1 = enhanced->m_activatedByPlayer1;
            state.hasP2 = true;
            state.p2 = enhanced->m_activatedByPlayer2;
        }

        return state;
    }

    template <class O>
    void restoreActivation(O* obj, const ActivationBits& state) {
        if (!obj) return;

        if constexpr (requires { obj->m_isActivated = false; }) {
            if (state.hasIsActivated) {
                obj->m_isActivated = state.isActivated;
            }
        }

        if (state.hasP1 || state.hasP2) {
            if (auto* enhanced = typeinfo_cast<EnhancedGameObject*>(obj)) {
                if (state.hasP1) enhanced->m_activatedByPlayer1 = state.p1;
                if (state.hasP2) enhanced->m_activatedByPlayer2 = state.p2;
            }
        }
    }

    // [FIX 4.1] Se captura UNA vez por decision y se restaura tras cada candidato
    // (antes se recorria y capturaba todo m_objects en cada candidato).
    struct ActivationGuard {
        std::vector<std::pair<GameObject*, ActivationBits>> saved;

        explicit ActivationGuard(PlayLayer* layer) {
            if (!layer || !layer->m_objects) return;
            saved.reserve(static_cast<size_t>(layer->m_objects->count()));
            for (auto* object : CCArrayExt<GameObject*>(layer->m_objects)) {
                if (object) {
                    saved.emplace_back(object, captureActivation(object));
                }
            }
        }

        void restore() {
            for (auto& entry : saved) {
                if (entry.first) {
                    restoreActivation(entry.first, entry.second);
                }
            }
        }

        ~ActivationGuard() {
            restore();
        }
    };

    template <class P>
    bool cloneOnSlope(P* player) {
        if (!player) return false;
        if constexpr (requires { player->m_isOnSlope; }) {
            return player->m_isOnSlope;
        }
        return false;
    }

    void applyGhostInput(bool down, int button) {
        if (!g_ghost) return;

        auto playerButton = static_cast<PlayerButton>(button);
        if (down) {
            g_ghost->pushButton(playerButton);
        } else {
            g_ghost->releaseButton(playerButton);
        }
    }

    // ------------------------------------------------------------------
    // Calculo del tick de aplicacion
    // ------------------------------------------------------------------

    // [FIX extra] Semantica exacta: delay 0 = proximo update; delay N = N updates despues.
    // Antes dueTick = g_tick + delay hacia que delay 0 y delay 1 fueran identicos
    // (ambos se aplicaban en el siguiente update) mientras el simulador los
    // distinguia -> desfase de 1 tick entre prediccion y realidad.
    int effectiveDue(int key, int delay) {
        int due = g_tick + 1 + delay;

        auto it = g_keyState.find(key);
        if (it != g_keyState.end() && it->second.pending > 0) {
            // [FIX 2.1/2.3] Solo se impone orden respecto al MISMO boton.
            due = std::max(due, it->second.lastDue + 1);
        }

        return due;
    }

    // ------------------------------------------------------------------
    // Simulacion
    // ------------------------------------------------------------------
    struct SimEvent {
        int step;
        bool down;
        int button;
    };

    SimResult simulateDelay(
        PlayLayer* layer,
        PlayerObject* real,
        bool down,
        int button,
        int candidateDue
    ) {
        if (!layer || !real || !g_ghost) {
            g_unknownReason = "no-ghost";
            return SimResult::Unknown;
        }

        // [FIX 4.3] dt inestable -> el horizonte no es fiable.
        if (g_dtStable < 2) {
            if (!g_warnedDt) {
                g_warnedDt = true;
                ac_console::line("[Assisted Click] NOTE dt not stable yet -> prediction skipped (Unknown)");
            }
            g_unknownReason = "dt-unstable";
            return SimResult::Unknown;
        }

        const int base = g_tick + 1;

        // [FIX extra] Eventos: cola pendiente real + candidato (antes la cola pendiente
        // no se tenia en cuenta en la simulacion).
        std::vector<SimEvent> events;
        events.reserve(g_queue.size() + 1);
        for (auto const& q : g_queue) {
            events.push_back({std::max(0, q.tick - base), q.down, q.button});
        }
        events.push_back({std::max(0, candidateDue - base), down, button});
        std::stable_sort(
            events.begin(), events.end(),
            [](SimEvent const& a, SimEvent const& b) { return a.step < b.step; }
        );

        const int lastStep = events.back().step;
        const int totalSteps = lastStep + kLookAhead;
        if (totalSteps > kMaxSimSteps) {
            g_unknownReason = "too-many-steps";
            return SimResult::Unknown;
        }

        const float simDt = g_stepDt > 0.0f ? g_stepDt : 0.25f;

        // Foto del jugador real para detectar mutaciones accidentales.
        const float realX = real->getPositionX();
        const float realY = real->getPositionY();
        const double realVy = real->m_yVelocity;

        SimResult result = SimResult::Safe;

        {
            SimulationScope scope;
            seedGhost(g_ghost, real);
            verifySeedOnce(real);

            size_t nextEvent = 0;

            for (int step = 0; step < totalSteps; ++step) {
                while (nextEvent < events.size() && events[nextEvent].step <= step) {
                    applyGhostInput(events[nextEvent].down, events[nextEvent].button);
                    ++nextEvent;
                }

                const float beforeX = g_ghost->getPositionX();
                g_ghost->update(simDt);
                const float afterX = g_ghost->getPositionX();

                // [FIX 3.2] Ya no se fuerza el avance del clon con g_lastDx.
                if (std::abs(afterX - beforeX) < 0.0001f &&
                    std::abs(g_lastDx) > 0.0001f) {
                    if constexpr (kForceXCompensation) {
                        g_ghost->setPositionX(beforeX + g_lastDx);
                    } else {
                        if (!g_warnedX) {
                            g_warnedX = true;
                            ac_console::line(
                                "[Assisted Click] WARNING ghost X does not advance "
                                "while real player does -> Unknown (input unchanged). "
                                "See kForceXCompensation in main.cpp."
                            );
                        }
                        g_unknownReason = "ghost-x-static";
                        result = SimResult::Unknown;
                        break;
                    }
                }

                const float collisionDt = cloneOnSlope(g_ghost) ? simDt : 0.0f;
                layer->checkCollisions(g_ghost, collisionDt, false);

                // [FIX 3.1] Orden del motor: update -> collisions -> rotation -> clear log.
                g_ghost->updateRotation(simDt);
                g_ghost->resetCollisionLog(false);

                // [FIX 3.4] Trigger = estado no modelado -> Unknown (no muerte).
                if (g_triggerEncountered) {
                    g_unknownReason = "trigger";
                    result = SimResult::Unknown;
                    break;
                }

                // [FIX 3.8] Muerte por hook o por flag del clon.
                if (g_cloneDied || g_ghost->m_isDead) {
                    result = SimResult::Died;
                    break;
                }
            }
        }

        // [FIX 3.5] Comprobar que el mundo real no cambio.
        if (std::abs(real->getPositionX() - realX) > 0.0001f ||
            std::abs(real->getPositionY() - realY) > 0.0001f ||
            std::abs(real->m_yVelocity - realVy) > 0.0001) {
            if (!g_warnedWorld) {
                g_warnedWorld = true;
                ac_console::line("[Assisted Click] WARNING simulation modified the real player state!");
            }
            g_unknownReason = "real-state-changed";
            return SimResult::Unknown;
        }

        return result;
    }

    // [FIX ghost lifetime] El fantasma se (re)crea bajo demanda y esta ligado a su
    // PlayLayer. Antes se destruia en PlayLayer::onExit, que cocos dispara tambien al
    // pausar / cambiar de escena, dejando g_ghost = NULL a mitad de nivel
    // (log: "ghost=NULL | clicksSeen=36 | predicted=0").
    bool ensureGhost(PlayLayer* layer) {
        if (!layer) return false;
        if (g_ghost && g_ghostOwner == layer) return true;

        // Fantasma de otra capa: la capa vieja puede estar ya destruida, asi que se
        // suelta el puntero SIN release (fuga minima, evita tocar memoria invalida).
        g_ghost = nullptr;
        g_ghostOwner = nullptr;

        auto* ghost = PlayerObject::create(1, 0, layer, layer, true);
        if (!ghost) {
            log::error("[AC] Ghost simulator creation failed");
            ac_console::line("[Assisted Click] ERROR: ghost creation FAILED -> no prediction");
            return false;
        }

        ghost->retain();
        ghost->setVisible(false);
        ghost->m_playEffects = false;

        g_ghost = ghost;
        g_ghostOwner = layer;

        ac_console::line("[Assisted Click] Ghost simulator (re)created");
        log::info("[AC] ghost (re)created");
        return true;
    }

    int predictDelay(
        PlayLayer* layer,
        PlayerObject* player,
        bool down,
        int button,
        bool isPlayer1
    ) {
        const int limit = windowTicks();

        if (!layer || !player || !isPlayer1 || limit <= 0 ||
            g_tick < 2 || player->m_isDead) {
            return 0;
        }

        if (!ensureGhost(layer)) {
            return 0;
        }

        const int key = buttonKey(button, isPlayer1);
        const auto t0 = std::chrono::steady_clock::now();

        int chosen = 0;
        ++g_clicksPredicted;
        g_unknownReason = "";
        ActivationGuard guard(layer);

        int prevDue = effectiveDue(key, 0);
        const SimResult baseline =
            simulateDelay(layer, player, down, button, prevDue);
        guard.restore();

        if (baseline == SimResult::Safe) {
            ++g_statKept;
            if constexpr (kVerboseClicks) {
                ac_console::line(
                    "[Assisted Click] CLICK " + std::string(down ? "PRESS" : "RELEASE") +
                    " | tick " + std::to_string(g_tick) + " | verdict=SAFE (unchanged)"
                );
            }
        }
        else if (baseline == SimResult::Unknown) {
            // [FIX 3.4] No inventar alternativas: se conserva la entrada original.
            ++g_statUnknown;
            if constexpr (kVerboseClicks) {
                ac_console::line(
                    "[Assisted Click] CLICK " + std::string(down ? "PRESS" : "RELEASE") +
                    " | tick " + std::to_string(g_tick) +
                    " | verdict=UNKNOWN (" + std::string(g_unknownReason) + ") -> unchanged"
                );
            }
        }
        else {
            bool found = false;

            for (int delay = 1; delay <= limit; ++delay) {
                const int due = effectiveDue(key, delay);
                if (due == prevDue) continue; // mismo tick efectivo, ya probado
                prevDue = due;

                const SimResult r = simulateDelay(layer, player, down, button, due);
                guard.restore();

                if (r == SimResult::Safe) {
                    chosen = delay;
                    found = true;
                    break;
                }
            }

            if (found) {
                ++g_statChanged;
                ac_console::line(
                    "[Assisted Click] DECISION | " +
                    std::string(down ? "PRESS" : "RELEASE") +
                    " | original predicted DEATH | delay +" +
                    std::to_string(chosen) +
                    " | tick " + std::to_string(g_tick) +
                    " | window " + std::to_string(limit)
                );
            } else {
                ++g_statNoSolution;
                ac_console::line(
                    "[Assisted Click] DECISION | " +
                    std::string(down ? "PRESS" : "RELEASE") +
                    " | original predicted DEATH | no safe delay in window " +
                    std::to_string(limit) + " -> unchanged"
                );
            }
        }

        const auto t1 = std::chrono::steady_clock::now();
        g_lastSimUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        g_maxSimUs = std::max(g_maxSimUs, g_lastSimUs);

        return chosen;
    }

    // ------------------------------------------------------------------
    // Cola
    // ------------------------------------------------------------------
    void queueInput(
        PlayLayer* layer,
        bool down,
        int button,
        bool isPlayer1,
        int delay
    ) {
        if (!layer) {
            return;
        }

        const int key = buttonKey(button, isPlayer1);

        // [FIX 2.2] Con OFF (delay 0) solo se envia directo si no hay nada pendiente
        // de ese boton y la cola esta vacia; si no, se mantiene el orden por la cola.
        auto keyIt = g_keyState.find(key);
        const bool keyPending =
            keyIt != g_keyState.end() && keyIt->second.pending > 0;

        if (delay == 0 && g_queue.empty() && !keyPending) {
            QueuedInputScope scope;
            layer->handleButton(down, button, isPlayer1);
            return;
        }

        // [FIX 2.1] dueTick definitivo (el mismo que uso el simulador).
        const int dueTick = effectiveDue(key, delay);

        // [FIX 2.3] Insercion ordenada y estable por tick; botones distintos
        // pueden compartir tick.
        auto pos = std::upper_bound(
            g_queue.begin(), g_queue.end(), dueTick,
            [](int value, PendingInput const& item) { return value < item.tick; }
        );
        g_queue.insert(pos, PendingInput{dueTick, down, button, isPlayer1});

        auto& state = g_keyState[key];
        state.lastDue = std::max(state.lastDue, dueTick);
        ++state.pending;

        ac_console::line(
            "[Assisted Click] SCHEDULED | " +
            std::string(down ? "PRESS" : "RELEASE") +
            " | button " + std::to_string(button) +
            " | tick " + std::to_string(g_tick) +
            " -> due " + std::to_string(dueTick) +
            " (+" + std::to_string(dueTick - (g_tick + 1)) + ")"
        );
    }

    void dispatchPendingInputs(PlayLayer* layer, int nextTick) {
        if (!layer) return;

        while (!g_queue.empty() &&
               g_queue.front().tick <= nextTick) {
            auto input = g_queue.front();
            g_queue.pop_front();

            // [FIX 2.1] Se libera la marca de pendiente al aplicar.
            const int key = buttonKey(input.button, input.isPlayer1);
            auto it = g_keyState.find(key);
            if (it != g_keyState.end()) {
                if (--it->second.pending <= 0) {
                    g_keyState.erase(it);
                }
            }

            {
                QueuedInputScope scope;
                layer->handleButton(
                    input.down,
                    input.button,
                    input.isPlayer1
                );
            }

            ac_console::line(
                "[Assisted Click] APPLIED | " +
                std::string(input.down ? "PRESS" : "RELEASE") +
                " | button " + std::to_string(input.button) +
                " | due " + std::to_string(input.tick) +
                " | applied at tick " + std::to_string(nextTick)
            );
        }
    }

    // Limpia cola y contadores de entrada (se llama antes Y despues del reset).
    void clearState() {
        g_queue.clear();
        g_keyState.clear();
        g_tick = 0;
        g_lastDx = 0.0f;
        g_stepDt = 0.25f;
        g_dtStable = 0;

        g_cloneDied = false;
        g_triggerEncountered = false;
        g_applyingQueuedInput = false;
    }

    void clearLevelDiagnostics() {
        g_seedVerified = false;
        g_warnedX = false;
        g_warnedDt = false;
        g_warnedWorld = false;
        g_statKept = 0;
        g_statChanged = 0;
        g_statNoSolution = 0;
        g_statUnknown = 0;
        g_lastSimUs = 0;
        g_maxSimUs = 0;
        g_loggedFirstUpdate = false;
        g_loggedFirstButton = false;
        g_clicksSeen = 0;
        g_clicksPredicted = 0;
        g_unknownReason = "";
    }

} // namespace ac

// ============================================================================
// Hooks
// ============================================================================
class $modify(ACKeyboard, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(
        cocos2d::enumKeyCodes key,
        bool down,
        bool repeat,
        double timestamp
    ) {
        if (key == cocos2d::enumKeyCodes::KEY_F6 && down && !repeat) {
            ac::g_windowIndex = (ac::g_windowIndex + 1) % 5;

            const int currentWindow = ac::windowTicks();

            if (currentWindow <= 0) {
                ac_console::line("[Assisted Click] F6 Cycle -> OFF (raw input)");
            } else {
                ac_console::line(
                    "[Assisted Click] F6 Cycle -> " +
                    std::to_string(currentWindow) +
                    " Ticks prediction"
                );
            }

            Notification::create(
                fmt::format("Assisted Click: {} ticks", currentWindow),
                NotificationIcon::None,
                1.0f
            )->show();
        }

        // [DIAG] F7: volcado de estado a consola Y al log de Geode.
        if (key == cocos2d::enumKeyCodes::KEY_F7 && down && !repeat) {
            auto* pl = PlayLayer::get();
            std::string msg =
                "[Assisted Click] STATUS | inLevel=" + std::string(pl ? "yes" : "NO") +
                " | window=" + std::to_string(ac::windowTicks()) +
                " | ghost=" + std::string(ac::g_ghost ? "ok" : "NULL") +
                " | tick=" + std::to_string(ac::g_tick) +
                " | dtStable=" + std::to_string(ac::g_dtStable) +
                " | clicksSeen=" + std::to_string(ac::g_clicksSeen) +
                " | predicted=" + std::to_string(ac::g_clicksPredicted) +
                " | kept=" + std::to_string(ac::g_statKept) +
                " chg=" + std::to_string(ac::g_statChanged) +
                " nosol=" + std::to_string(ac::g_statNoSolution) +
                " unk=" + std::to_string(ac::g_statUnknown) +
                " | lastUnknown=" + std::string(ac::g_unknownReason);
            ac_console::line(msg);
            log::info("{}", msg);
        }

        return CCKeyboardDispatcher::dispatchKeyboardMSG(
            key, down, repeat, timestamp
        );
    }
};

class $modify(ACPlayLayer, PlayLayer) {
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();

        // [FIX 5] Reintento de consola si fallo en Loaded.
        ac_console::showBannerOnce();

        ac::g_layer = this;
        ac::clearState();
        ac::clearLevelDiagnostics();

        // Fantasma anterior (de otro nivel): se suelta sin release, su capa ya no existe.
        ac::g_ghost = nullptr;
        ac::g_ghostOwner = nullptr;
        ac::ensureGhost(this);
    }

    void resetLevel() {
        // [FIX 2.4] Invalidar cola ANTES del reinicio y de nuevo despues.
        ac::clearState();
        PlayLayer::resetLevel();
        ac::clearState();
    }

    // [FIX ghost lifetime] onExit tambien se llama al pausar / cambiar de escena:
    // NO se destruye nada aqui, solo se invalida la cola de entradas pendientes.
    void onExit() {
        ac::g_queue.clear();
        ac::g_keyState.clear();
        PlayLayer::onExit();
    }

    // Salida real del nivel: aqui si se libera el fantasma (la capa sigue viva).
    void onQuit() {
        ac::clearState();

        auto* ghost = ac::g_ghost;
        ac::g_ghost = nullptr;
        ac::g_ghostOwner = nullptr;
        ac::g_layer = nullptr;

        if (ghost) {
            ghost->release();
        }

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

class $modify(ACBaseLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (ac::g_applyingQueuedInput || ac::g_simulating) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        auto* layer = PlayLayer::get();

        ++ac::g_clicksSeen;
        if (!ac::g_loggedFirstButton) {
            ac::g_loggedFirstButton = true;
            const std::string m =
                "[Assisted Click] First handleButton | down=" + std::to_string(down) +
                " button=" + std::to_string(button) +
                " p1=" + std::to_string(isPlayer1) +
                " playLayer=" + std::string(layer ? "yes" : "NO") +
                " sameLayer=" + std::string(
                    (layer && static_cast<GJBaseGameLayer*>(layer) ==
                              static_cast<GJBaseGameLayer*>(this)) ? "yes" : "NO") +
                " window=" + std::to_string(ac::windowTicks()) +
                " ghost=" + std::string(ac::g_ghost ? "ok" : "NULL");
            ac_console::line(m);
            log::info("{}", m);
        }

        // [FIX 2.5] Solo interceptar si ESTA capa es el PlayLayer activo.
        if (!layer ||
            static_cast<GJBaseGameLayer*>(layer) !=
                static_cast<GJBaseGameLayer*>(this)) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        // Jugador 2: se reenvia sin tocar (selector original conservado).
        if (!isPlayer1) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        auto* player = layer->m_player1;

        // [FIX 2.1] Ya no hay g_pressDelay: la prediccion de cada evento es
        // independiente y el orden lo impone effectiveDue().
        int delay = 0;
        if (player && ac::windowTicks() > 0) {
            delay = ac::predictDelay(layer, player, down, button, isPlayer1);
        }

        // [FIX 2.2] Aun con OFF (delay 0) se respeta la cola pendiente.
        ac::queueInput(layer, down, button, isPlayer1, delay);
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
            // [FIX 4.3] Estabilidad del dt
            if (std::abs(dt - ac::g_stepDt) < 0.00002f) {
                ++ac::g_dtStable;
            } else {
                ac::g_dtStable = 0;
            }

            ac::g_stepDt = dt;
            ac::g_lastDx = getPositionX() - beforeX;
            ++ac::g_tick;

            if (!ac::g_loggedFirstUpdate) {
                ac::g_loggedFirstUpdate = true;
                const std::string m =
                    "[Assisted Click] First real P1 update | dt=" + std::to_string(dt) +
                    " dx=" + std::to_string(ac::g_lastDx);
                ac_console::line(m);
                log::info("{}", m);
            }

            // [FIX 5] Refresco visual cada 4 ticks (el contador interno va cada paso).
            if ((ac::g_tick & 3) == 0) {
                ac_console::tickLine(
                    "[AC] TICK " + std::to_string(ac::g_tick) +
                    " | x=" + std::to_string(getPositionX()) +
                    " | kept=" + std::to_string(ac::g_statKept) +
                    " chg=" + std::to_string(ac::g_statChanged) +
                    " nosol=" + std::to_string(ac::g_statNoSolution) +
                    " unk=" + std::to_string(ac::g_statUnknown) +
                    " | sim=" + std::to_string(ac::g_lastSimUs) + "us" +
                    " max=" + std::to_string(ac::g_maxSimUs) + "us"
                );
            }
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
            // No se ejecuta durante la simulacion; marca el resultado como Unknown.
            ac::g_triggerEncountered = true;
            return;
        }

        EffectGameObject::triggerObject(layer, id, groups);
    }
};
