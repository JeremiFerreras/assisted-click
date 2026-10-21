// Assisted Click - src/main.cpp -- v1.2.1 (false-positive and runtime-budget hardening)
//
// Que hace: al recibir un clic (PRESS/RELEASE) simula un jugador "falso" tick a tick
// (paso de 1/240 s = 0.25 frames @ 60fps). Si con el timing original el falso muere,
// busca el menor retraso dentro del limite de la etapa con el que consiga pasar el hueco
// y aplica la entrada retrasada.
//
// Tecnicas tomadas de xdBot (ZiLko/xdBot):
//  - jugador falso creado con PlayerObject::create y anadido a m_objectLayer
//  - copia amplia del estado de PlayerObject mediante seedGhost/AC_COPY; no se asume completa
//  - por paso: limpiar logs -> checkCollisions -> aplicar entrada -> update -> rotacion/escala
//  - mientras se simula se bloquean TODOS los efectos secundarios (sfx, triggers, aros,
//    particulas, estela, animacion de fin de nivel...) y los objetos no modelados se
//    desactivan solo durante la llamada de colisiones (collisionCheckObjects).
//
// Uso personal / offline. No sirve para enviar records a listas.
//
// Consola: banner, ciclos F6, estado F7 y eventos "SAVED"; el diagnóstico detallado va al log de Geode.

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
#include <optional>
#include <string>
#include <limits>

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/modify/EffectGameObject.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/HardStreak.hpp>

#include <algorithm>
#include <cmath>
#include <deque>
#include <unordered_map>
#include <unordered_set>
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

static bool init() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_ready) return true;

    // Never detach/free a console owned by the host or another mod.
    // Create one only when the process is not already attached to a console.
    if (GetConsoleWindow() == nullptr && !AllocConsole()) return false;

    g_output = GetStdHandle(STD_OUTPUT_HANDLE);
    if (g_output == nullptr || g_output == INVALID_HANDLE_VALUE) {
        g_output = INVALID_HANDLE_VALUE;
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

    // Ventana CRITICA: ticks que se simulan tras aplicar la entrada. La simulacion mantiene
    // el boton igual todo ese tiempo, pero el jugador real lo cambia pronto (en wave cada
    // pocos frames). Con 28-64 ticks el fantasma moria siempre al chocar con techo/suelo por
    // "mantener pulsado" (log: DEATH at step 22-26 sin importar el retraso) y nunca habia
    // un retraso "seguro". Las muertes que SI dependen del timing del clic ocurren en los
    // primeros ticks (el video de referencia muestra Frame0: 8). 16 ticks = 4 frames @60fps.
    // Short baseline lookahead: this is only a trigger for searching, NOT proof that
    // a candidate is safe. Candidate validation runs until it clears the baseline hazard.
    constexpr int kHorizon = 16;
    constexpr int kMaxSimSteps = 96;    // hard cap per trajectory
    constexpr int kMaxStepsPerInput = 512; // aggregate budget across all candidate trajectories
    constexpr int kMaxClearanceEstimate = 48; // far targets fail closed rather than stall a frame
    constexpr int kPostClearanceSteps = 2; // also sample collisions just after the target edge
    constexpr float kHazardClearancePadding = 8.0f;
    constexpr float kMinConservativeHalfWidth = 20.0f; // covers normal-size and rotating hitboxes
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
        ac_console::line("[InputBuffer] v1.2.1 LOADED (fail-closed predictive core)");
        ac_console::line(fmt::format(
            "[InputBuffer] Starting Mode: STAGE 1 ({} All / {} Wave)", s.all, s.wave
        ));
        ac_console::line("[InputBuffer] Press F6 in-level to cycle: 2/2 -> 4/4 -> 8/8 -> 16/8 -> OFF");
        ac_console::line("===================================================");
    }

    int limitFor(PlayerObject* p) {
        auto const& s = kStages[g_stage];
        return (p && p->m_isDart) ? s.wave : s.all;
    }

    // ---- Estado de la partida
    int g_tick = 0;           // updates reales del jugador 1 con dt > 0
    float g_stepDt = 0.25f;   // ultimo dt real (xdBot: t.delta)
    float g_lastDx = 0.0f;
    int g_dtStable = 0;

    PlayLayer* g_layer = nullptr;
    PlayerObject* g_ghost = nullptr;
    PlayLayer* g_ghostOwner = nullptr;

    bool g_simulating = false;
    bool g_cloneDied = false;
    bool g_unmodeled = false; // la simulacion toco algo que no sabe modelar
    bool g_applyingQueuedInput = false;

    // ---- Diagnostico (solo log de Geode)
    const char* g_unknownReason = "";
    int g_clicksSeen = 0, g_clicksPredicted = 0;
    int g_skipOff = 0, g_skipEarly = 0, g_skipDead = 0, g_skipNoGhost = 0, g_skipOther = 0;
    int g_statKept = 0, g_statSaved = 0, g_statNoSolution = 0, g_statUnknown = 0;
    bool g_warnedX = false, g_warnedSeed = false, g_warnedWorld = false;
    bool g_warnedFallbackX = false;

    bool g_loggedFirstUpdate = false;

    std::string statusLine(const char* tag) {
        return fmt::format(
            "[IB] {} stage={} ghost={} tick={} dtStable={} clicks={} predicted={} kept={} saved={} "
            "nosol={} unknown={} lastUnknown={} | skipped: off={} early={} dead={} noGhost={} other={}",
            tag, g_stage + 1, g_ghost ? "ok" : "NULL", g_tick, g_dtStable, g_clicksSeen,
            g_clicksPredicted, g_statKept, g_statSaved, g_statNoSolution, g_statUnknown,
            g_unknownReason, g_skipOff, g_skipEarly, g_skipDead, g_skipNoGhost, g_skipOther
        );
    }

    enum class SimResult { Safe, Died, Unknown };

    struct SimOut {
        SimResult result;
        int steps;
        float deathX = 0.0f;
        float deathObjectRight = 0.0f;
        bool knownPhysicalDeathObject = false;
        GameObject* deathObject = nullptr;
        float endX = 0.0f;
        float endY = 0.0f;
        float endYVelocity = 0.0f;
        float endRotation = 0.0f;
        bool endIsDart = false;
        bool endIsShip = false;
        bool endIsBall = false;
        bool endIsBird = false;
        bool endIsRobot = false;
        bool endIsSpider = false;
        bool endIsSwing = false;
        bool endIsUpsideDown = false;
    };

    // Populated only when the clone dies inside PlayLayer::destroyPlayer.
    float g_cloneDeathX = 0.0f;
    float g_cloneDeathObjectRight = 0.0f;
    bool g_cloneDeathObjectKnown = false;
    GameObject* g_cloneDeathObject = nullptr;

    // ---- Cola de entradas
    struct PendingInput {
        int tick;
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
            g_unmodeled = false;
            g_cloneDeathX = 0.0f;
            g_cloneDeathObjectRight = 0.0f;
            g_cloneDeathObjectKnown = false;
            g_cloneDeathObject = nullptr;
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
        g_unmodeled = false;
        g_applyingQueuedInput = false;
    }

    void clearStats() {
        g_unknownReason = "";
        g_clicksSeen = g_clicksPredicted = 0;
        g_skipOff = g_skipEarly = g_skipDead = g_skipNoGhost = g_skipOther = 0;
        g_loggedFirstUpdate = false;
        g_statKept = g_statSaved = g_statNoSolution = g_statUnknown = 0;
        g_warnedX = g_warnedSeed = g_warnedWorld = false;
        g_warnedFallbackX = false;
    }

    // ========================================================================
    // Objetos modelados / no modelados (misma idea que xdBot: objectTypes + portalIDs)
    // ========================================================================
    // Portales que SI se modelan (tamano y velocidad), igual que ShowTrajectory::handlePortal.
    const std::unordered_set<int> kModeledPortals = { 101, 99, 200, 201, 202, 203, 1334 };

    // Fisicos: bloques, picos, picos animados, pendientes.
    bool isPhysical(GameObject* o) {
        switch (o->m_objectType) {
            case GameObjectType::Solid:
            case GameObjectType::Hazard:
            case GameObjectType::AnimatedHazard:
            case GameObjectType::Slope:
                return true;
            default:
                return false;
        }
    }

    bool isModeled(GameObject* o) {
        return isPhysical(o) || kModeledPortals.contains(o->m_objectID);
    }

    // Sin efecto en la fisica (decoracion, monedas...): se desactivan sin marcar Unknown.
    bool isHarmless(GameObject* o) {
        switch (o->m_objectType) {
            case GameObjectType::Decoration:
            case GameObjectType::SecretCoin:
            case GameObjectType::UserCoin:
            case GameObjectType::Collectible:
                return true;
            default:
                return false;
        }
    }

    // La predicción de limpieza solo puede usar como objetivo una colisión física estable.
    // Los peligros animados o los objetos físicos con acciones corriendo pueden moverse mientras
    // el nivel real avanza; el clon NO actualiza el mundo, así que se rechazan conservadoramente.
    bool isClearancePhysical(GameObject* o) {
        if (!o) return false;
        switch (o->m_objectType) {
            case GameObjectType::Solid:
            case GameObjectType::Hazard:
            case GameObjectType::Slope:
                return true;
            default:
                return false;
        }
    }

    bool hasDynamicPhysicalMotion(GameObject* o) {
        if (!o || !isPhysical(o)) return false;
        if (o->m_objectType == GameObjectType::AnimatedHazard) return true;
        return o->numberOfRunningActions() > 0;
    }

    bool hasDynamicPhysicalObjectInXRange(PlayLayer* layer, float minX, float maxX) {
        if (!layer || !layer->m_objects || !std::isfinite(minX) || !std::isfinite(maxX)) {
            return true; // unavailable inspection is not proof of a static world
        }
        for (auto* obj : CCArrayExt<GameObject*>(layer->m_objects)) {
            if (!obj || !isPhysical(obj)) continue;
            const auto rect = obj->getObjectRect();
            if (rect.getMaxX() < minX || rect.getMinX() > maxX) continue;
            if (hasDynamicPhysicalMotion(obj)) return true;
        }
        return false;
    }

    // Igual que ShowTrajectory::handlePortal (gravedad omitida igual que en xdBot).
    void handlePortal(PlayerObject* player, int id) {
        switch (id) {
            case 101: player->togglePlayerScale(true, true);  player->updatePlayerScale(); break;
            case 99:  player->togglePlayerScale(false, true); player->updatePlayerScale(); break;
            case 200: player->m_playerSpeed = 0.7f; break;
            case 201: player->m_playerSpeed = 0.9f; break;
            case 202: player->m_playerSpeed = 1.1f; break;
            case 203: player->m_playerSpeed = 1.3f; break;
            case 1334: player->m_playerSpeed = 1.6f; break;
            default: break;
        }
    }

    // ========================================================================
    // Copia del estado del jugador (PlayerPracticeFixes::saveData/applyData)
    // ========================================================================
    // Cada AC_COPY solo compila si el miembro existe en los bindings 2.2081 (xdBot esta
    // hecho para 2.2074; en 2.2081 faltan p.ej. m_currentSlope3, m_unk9e8, m_unkAAC...).
    // NO se copian punteros a objetos propios del jugador real (arrays de colision,
    // m_touchingRings, particulas, estela): el falso usa los suyos.
    template <class P>
    void seedGhost(P* dst, P* src) {
        if (!dst || !src) return;

        // Clear clone-only transient collision state before seeding it from the real player.
        // The requires checks keep this source compatible with binding variations.
        if constexpr (requires { dst->resetCollisionLog(true); }) {
            dst->resetCollisionLog(true);
        }
        if constexpr (requires { dst->resetCollisionValues(); }) {
            dst->resetCollisionValues();
        }

        dst->setPosition(src->getPosition());
        dst->setRotation(src->getRotation());
        dst->setScale(src->getScale());

        #define AC_COPY(F) \
            if constexpr (requires { dst->F = src->F; }) { dst->F = src->F; }

        // contenedores de gd:: (como en xdBot: solo Windows)
        #ifdef GEODE_IS_WINDOWS
        AC_COPY(m_rotateObjectsRelated)
        AC_COPY(m_maybeRotatedObjectsMap)
        AC_COPY(m_touchedRings)
        AC_COPY(m_ringRelatedSet)
        AC_COPY(m_jumpPadRelated)
        AC_COPY(m_holdingButtons)
        AC_COPY(m_playerFollowFloats)
        #endif

        AC_COPY(position) AC_COPY(rotation) AC_COPY(m_holdingRight) AC_COPY(m_holdingLeft)
        AC_COPY(m_wasTeleported) AC_COPY(m_fixGravityBug) AC_COPY(m_reverseSync) AC_COPY(m_yVelocityBeforeSlope)
        AC_COPY(m_dashX) AC_COPY(m_dashY) AC_COPY(m_dashAngle) AC_COPY(m_dashStartTime)
        AC_COPY(m_dashRing) AC_COPY(m_slopeStartTime) AC_COPY(m_maybeLastGroundObject) AC_COPY(m_lastCollisionBottom)
        AC_COPY(m_lastCollisionTop) AC_COPY(m_lastCollisionLeft) AC_COPY(m_lastCollisionRight) AC_COPY(m_unk50C)
        AC_COPY(m_unk510) AC_COPY(m_currentSlope2) AC_COPY(m_preLastGroundObject) AC_COPY(m_slopeAngle)
        AC_COPY(m_slopeSlidingMaybeRotated) AC_COPY(m_quickCheckpointMode) AC_COPY(m_collidedObject) AC_COPY(m_lastGroundObject)
        AC_COPY(m_collidingWithLeft) AC_COPY(m_collidingWithRight) AC_COPY(m_scaleXRelated2) AC_COPY(m_groundYVelocity)
        AC_COPY(m_yVelocityRelated) AC_COPY(m_scaleXRelated3) AC_COPY(m_scaleXRelated4) AC_COPY(m_scaleXRelated5)
        AC_COPY(m_isCollidingWithSlope) AC_COPY(m_isBallRotating) AC_COPY(m_unk669) AC_COPY(m_currentSlope3)
        AC_COPY(m_currentSlope) AC_COPY(unk_584) AC_COPY(m_collidingWithSlopeId) AC_COPY(m_slopeFlipGravityRelated)
        AC_COPY(m_slopeAngleRadians) AC_COPY(m_rotationSpeed) AC_COPY(m_rotateSpeed) AC_COPY(m_isRotating)
        AC_COPY(m_isBallRotating2) AC_COPY(m_speedMultiplier) AC_COPY(m_yStart) AC_COPY(m_gravity)
        AC_COPY(m_unk648) AC_COPY(m_gameModeChangedTime) AC_COPY(m_padRingRelated) AC_COPY(m_maybeIsFalling)
        AC_COPY(m_shouldTryPlacingCheckpoint) AC_COPY(m_maybeCanRunIntoBlocks) AC_COPY(m_isOnGround3) AC_COPY(m_checkpointTimeout)
        AC_COPY(m_lastCheckpointTime) AC_COPY(m_lastJumpTime) AC_COPY(m_lastFlipTime) AC_COPY(m_lastSpiderFlipTime)
        AC_COPY(m_unkBool5) AC_COPY(m_accelerationOrSpeed) AC_COPY(m_snapDistance) AC_COPY(m_ringJumpRelated)
        AC_COPY(m_objectSnappedTo) AC_COPY(m_onFlyCheckpointTries) AC_COPY(m_slopeRotation) AC_COPY(m_currentSlopeYVelocity)
        AC_COPY(m_unk3d0) AC_COPY(m_blackOrbRelated) AC_COPY(m_unk3e0) AC_COPY(m_unk3e1)
        AC_COPY(m_isAccelerating) AC_COPY(m_isCurrentSlopeTop) AC_COPY(m_collidedTopMinY) AC_COPY(m_collidedBottomMaxY)
        AC_COPY(m_collidedLeftMaxX) AC_COPY(m_collidedRightMinX) AC_COPY(m_canPlaceCheckpoint) AC_COPY(m_maybeIsColliding)
        AC_COPY(m_jumpBuffered) AC_COPY(m_stateRingJump) AC_COPY(m_wasJumpBuffered) AC_COPY(m_wasRobotJump)
        AC_COPY(m_stateJumpBuffered) AC_COPY(m_stateRingJump2) AC_COPY(m_touchedRing) AC_COPY(m_touchedCustomRing)
        AC_COPY(m_touchedGravityPortal) AC_COPY(m_maybeTouchedBreakableBlock) AC_COPY(m_jumpRelatedAC2) AC_COPY(m_touchedPad)
        AC_COPY(m_yVelocity) AC_COPY(m_fallSpeed) AC_COPY(m_isOnSlope) AC_COPY(m_wasOnSlope)
        AC_COPY(m_slopeVelocity) AC_COPY(m_maybeUpsideDownSlope) AC_COPY(m_isShip) AC_COPY(m_isBird)
        AC_COPY(m_isBall) AC_COPY(m_isDart) AC_COPY(m_isRobot) AC_COPY(m_isSpider)
        AC_COPY(m_isUpsideDown) AC_COPY(m_isOnGround) AC_COPY(m_isGoingLeft) AC_COPY(m_isSideways)
        AC_COPY(m_isSwing) AC_COPY(m_reverseRelated) AC_COPY(m_maybeReverseSpeed) AC_COPY(m_maybeReverseAcceleration)
        AC_COPY(m_xVelocityRelated2) AC_COPY(m_isDashing) AC_COPY(m_unk9e8) AC_COPY(m_groundObjectMaterial)
        AC_COPY(m_vehicleSize) AC_COPY(m_playerSpeed) AC_COPY(m_shipRotation) AC_COPY(m_lastPortalPos)
        AC_COPY(m_unkUnused3) AC_COPY(m_isOnGround2) AC_COPY(m_lastLandTime) AC_COPY(m_platformerVelocityRelated)
        AC_COPY(m_maybeIsBoosted) AC_COPY(m_scaleXRelatedTime) AC_COPY(m_decreaseBoostSlide) AC_COPY(m_unkA29)
        AC_COPY(m_isLocked) AC_COPY(m_controlsDisabled) AC_COPY(m_lastGroundedPos) AC_COPY(m_lastActivatedPortal)
        AC_COPY(m_hasEverJumped) AC_COPY(m_ringOrStreakRelated) AC_COPY(m_position) AC_COPY(m_isSecondPlayer)
        AC_COPY(m_unkA99) AC_COPY(m_totalTime) AC_COPY(m_isBeingSpawnedByDualPortal) AC_COPY(m_unkAAC)
        AC_COPY(m_unkAngle1) AC_COPY(m_yVelocityRelated3) AC_COPY(m_followRelated) AC_COPY(m_unk838)
        AC_COPY(m_stateOnGround) AC_COPY(m_stateUnk) AC_COPY(m_stateNoStickX) AC_COPY(m_stateNoStickY)
        AC_COPY(m_stateUnk2) AC_COPY(m_stateBoostX) AC_COPY(m_stateBoostY) AC_COPY(m_maybeStateForce2)
        AC_COPY(m_stateScale) AC_COPY(m_platformerXVelocity) AC_COPY(m_leftPressedFirst) AC_COPY(m_scaleXRelated)
        AC_COPY(m_maybeHasStopped) AC_COPY(m_xVelocityRelated) AC_COPY(m_maybeGoingCorrectSlopeDirection) AC_COPY(m_isSliding)
        AC_COPY(m_maybeSlopeForce) AC_COPY(m_isOnIce) AC_COPY(m_physDeltaRelated) AC_COPY(m_isOnGround4)
        AC_COPY(m_maybeSlidingTime) AC_COPY(m_maybeSlidingStartTime) AC_COPY(m_changedDirectionsTime) AC_COPY(m_slopeEndTime)
        AC_COPY(m_isMoving) AC_COPY(m_platformerMovingLeft) AC_COPY(m_platformerMovingRight) AC_COPY(m_isSlidingRight)
        AC_COPY(m_maybeChangedDirectionAngle) AC_COPY(m_unkUnused2) AC_COPY(m_isPlatformer) AC_COPY(m_stateNoAutoJump)
        AC_COPY(m_stateDartSlide) AC_COPY(m_stateHitHead) AC_COPY(m_stateFlipGravity) AC_COPY(m_gravityMod)
        AC_COPY(m_stateForce) AC_COPY(m_stateForceVector) AC_COPY(m_affectedByForces) AC_COPY(m_somethingPlayerSpeedTime)
        AC_COPY(m_playerSpeedAC) AC_COPY(m_fixRobotJump) AC_COPY(m_inputsLocked) AC_COPY(m_unk958)
        AC_COPY(m_unkUnused) AC_COPY(m_isOutOfBounds) AC_COPY(m_fallStartY) AC_COPY(m_disablePlayerSqueeze)
        AC_COPY(m_robotHasRun3) AC_COPY(m_robotHasRun2) AC_COPY(m_ignoreDamage) AC_COPY(m_enable22Changes)

        #undef AC_COPY

        dst->m_isDead = false;
        dst->m_playEffects = false;
    }

    // Igual que xdBot: dos PlayerObject::create y anadidos a m_objectLayer. Aqui solo uno.
    // Vive ligado a su PlayLayer; se libera en PlayLayer::onQuit (NO en onExit).
    bool ensureGhost(PlayLayer* layer) {
        if (!layer || !layer->m_objectLayer) return false;
        if (g_ghost && g_ghostOwner == layer) return true;

        // The retained clone can outlive its old layer. Release our retain without
        // dereferencing g_ghostOwner (which may already be stale).
        if (g_ghost) {
            auto* staleGhost = g_ghost;
            g_ghost = nullptr;
            g_ghostOwner = nullptr;
            staleGhost->removeFromParentAndCleanup(true);
            staleGhost->release();
        }

        auto* ghost = PlayerObject::create(1, 1, layer, layer, true);
        if (!ghost) {
            log::error("[IB] ghost creation failed");
            return false;
        }

        ghost->retain();
        ghost->setPosition({0, 105});
        ghost->setVisible(false);
        ghost->m_playEffects = false;
        if (layer->m_objectLayer) {
            layer->m_objectLayer->addChild(ghost);
        }

        g_ghost = ghost;
        g_ghostOwner = layer;
        log::info("[IB] ghost created");
        return true;
    }

    void destroyGhost() {
        auto* ghost = g_ghost;
        g_ghost = nullptr;
        g_ghostOwner = nullptr;
        if (ghost) {
            ghost->removeFromParentAndCleanup(true);
            ghost->release();
        }
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
    // Tick de aplicacion
    // ========================================================================
    // delay 0 = proximo update del jugador; delay N = N updates despues.
    int effectiveDue(int key, int delay) {
        int due = g_tick + 1 + delay;
        auto it = g_keys.find(key);
        if (it != g_keys.end() && it->second.pending > 0) {
            due = std::max(due, it->second.lastDue + 1);
        }
        return due;
    }

    // ========================================================================
    // Simulacion (misma secuencia que ShowTrajectory::createTrajectory)
    // ========================================================================
    struct SimEvent { int step; bool down; int button; bool candidate; };

    static SimOut unknown(const char* why) {
        g_unknownReason = why;
        return {SimResult::Unknown, 0};
    }

    template <class P>
    void setCloneX(P* player, float x) {
        player->setPositionX(x);
        // GD maintains a separate position member in some bindings/builds.
        if constexpr (requires { player->m_position.x = x; }) {
            player->m_position.x = x;
        }
    }

    bool nearFloat(float a, float b, float tolerance) {
        return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) <= tolerance;
    }

    bool sameBaselineDeath(SimOut const& a, SimOut const& b) {
        return a.result == SimResult::Died && b.result == SimResult::Died &&
            a.steps == b.steps && a.knownPhysicalDeathObject && b.knownPhysicalDeathObject &&
            a.deathObject == b.deathObject &&
            nearFloat(a.deathX, b.deathX, 0.05f) &&
            nearFloat(a.deathObjectRight, b.deathObjectRight, 0.05f);
    }

    bool sameSafeClearance(SimOut const& a, SimOut const& b) {
        return a.result == SimResult::Safe && b.result == SimResult::Safe &&
            a.steps == b.steps &&
            nearFloat(a.endX, b.endX, 0.05f) &&
            nearFloat(a.endY, b.endY, 0.05f) &&
            nearFloat(a.endYVelocity, b.endYVelocity, 0.1f) &&
            nearFloat(a.endRotation, b.endRotation, 0.1f) &&
            a.endIsDart == b.endIsDart && a.endIsShip == b.endIsShip &&
            a.endIsBall == b.endIsBall && a.endIsBird == b.endIsBird &&
            a.endIsRobot == b.endIsRobot && a.endIsSpider == b.endIsSpider &&
            a.endIsSwing == b.endIsSwing && a.endIsUpsideDown == b.endIsUpsideDown;
    }

    SimOut simulate(
        PlayLayer* layer,
        PlayerObject* real,
        bool down,
        int button,
        int candidateDue,
        std::optional<float> clearTargetX = std::nullopt,
        int stepBudget = kMaxSimSteps
    ) {
        if (!layer || !real || !g_ghost) return unknown("no-ghost");
        if (g_dtStable < 2) return unknown("dt-unstable");
        if (!std::isfinite(g_stepDt) || g_stepDt <= 0.0f) return unknown("invalid-dt");
        if (stepBudget <= 0) return unknown("per-click-budget-exhausted");
        if (!g_ghost->m_collisionLogTop || !g_ghost->m_collisionLogBottom ||
            !g_ghost->m_collisionLogLeft || !g_ghost->m_collisionLogRight) {
            return unknown("ghost-collision-log-null");
        }

        const int base = g_tick + 1;
        std::vector<SimEvent> events;
        events.reserve(g_queue.size() + 1);
        for (auto const& q : g_queue) {
            events.push_back({std::max(0, q.tick - base), q.down, q.button, false});
        }
        events.push_back({std::max(0, candidateDue - base), down, button, true});
        std::stable_sort(events.begin(), events.end(),
            [](SimEvent const& a, SimEvent const& b) { return a.step < b.step; });

        const int latestEventStep = events.back().step;
        if (latestEventStep >= kMaxSimSteps) return unknown("too-many-steps");

        int plannedSteps = latestEventStep + kHorizon + 1;
        if (clearTargetX) {
            // A target whose distance cannot be estimated reliably is not a safe prediction.
            float dxPerTick = std::abs(g_lastDx);
            if (!std::isfinite(dxPerTick) || dxPerTick < 0.001f) {
                dxPerTick = static_cast<float>(std::abs(real->getCurrentXVelocity() * g_stepDt));
            }
            if (!std::isfinite(dxPerTick) || dxPerTick < 0.001f) {
                return unknown("clearance-speed-unknown");
            }
            const float distance = std::max(0.0f, *clearTargetX - real->getPositionX());
            const int estimatedTicks = static_cast<int>(std::ceil(distance / dxPerTick));
            if (estimatedTicks > kMaxClearanceEstimate) {
                return unknown("clearance-target-too-far");
            }
            // Speed portals can slow the clone after the estimate. Use a generous multiplier,
            // but keep a strict hard cap so one click cannot freeze the game for a long sweep.
            const int modeLimit = std::max(0, limitFor(real));
            plannedSteps = std::min(
                kMaxSimSteps,
                std::max(kHorizon + modeLimit + 8, estimatedTicks * 3 + modeLimit + 8)
            );
        }
        const int totalSteps = std::min(plannedSteps, stepBudget);
        const bool budgetTruncated = totalSteps < plannedSteps;
        const float dt = g_stepDt;
        const float rx = real->getPositionX();
        const float ry = real->getPositionY();
        const double rv = real->m_yVelocity;

        SimOut out{SimResult::Safe, totalSteps};
        bool candidateApplied = false;
        bool clearedTarget = false;
        int targetCrossStep = -1;

        {
            SimulationScope scope;
            seedGhost(g_ghost, real);

            const bool seedOK =
                nearFloat(g_ghost->getPositionX(), rx, 0.001f) &&
                nearFloat(g_ghost->getPositionY(), ry, 0.001f) &&
                nearFloat(static_cast<float>(g_ghost->m_yVelocity), static_cast<float>(rv), 0.001f) &&
                nearFloat(g_ghost->getRotation(), real->getRotation(), 0.05f) &&
                nearFloat(g_ghost->getScale(), real->getScale(), 0.001f) &&
                nearFloat(g_ghost->m_playerSpeed, real->m_playerSpeed, 0.0001f) &&
                nearFloat(g_ghost->m_speedMultiplier, real->m_speedMultiplier, 0.0001f) &&
                g_ghost->m_isDart == real->m_isDart &&
                g_ghost->m_isShip == real->m_isShip &&
                g_ghost->m_isBall == real->m_isBall &&
                g_ghost->m_isBird == real->m_isBird &&
                g_ghost->m_isRobot == real->m_isRobot &&
                g_ghost->m_isSpider == real->m_isSpider &&
                g_ghost->m_isSwing == real->m_isSwing &&
                g_ghost->m_isUpsideDown == real->m_isUpsideDown &&
                g_ghost->m_isPlatformer == real->m_isPlatformer;

            if (!g_warnedSeed) {
                g_warnedSeed = true;
                log::info("[IB] first seed check: {}", seedOK ? "OK" : "MISMATCH");
            }

            if (!seedOK) {
                out = unknown("ghost-seed-mismatch");
            } else {
                size_t next = 0;
                for (int step = 0; step < totalSteps; ++step) {
                    // Same engine phase ordering as xdBot: collision sample, input, physics.
                    g_ghost->m_collisionLogTop->removeAllObjects();
                    g_ghost->m_collisionLogBottom->removeAllObjects();
                    g_ghost->m_collisionLogLeft->removeAllObjects();
                    g_ghost->m_collisionLogRight->removeAllObjects();
                    layer->checkCollisions(g_ghost, dt, false);

                    if (g_unmodeled) {
                        out = unknown("unmodeled-object-or-trigger");
                        out.steps = step + 1;
                        break;
                    }
                    if (g_cloneDied || g_ghost->m_isDead) {
                        out = {SimResult::Died, step + 1};
                        out.deathX = g_cloneDeathX;
                        out.deathObjectRight = g_cloneDeathObjectRight;
                        out.knownPhysicalDeathObject = g_cloneDeathObjectKnown;
                        out.deathObject = g_cloneDeathObject;
                        break;
                    }

                    if (clearTargetX && candidateApplied &&
                        g_ghost->getPositionX() >= *clearTargetX) {
                        if (targetCrossStep < 0) targetCrossStep = step;
                        if (step - targetCrossStep >= kPostClearanceSteps) {
                            out = {SimResult::Safe, step + 1};
                            clearedTarget = true;
                            break;
                        }
                    }

                    while (next < events.size() && events[next].step <= step) {
                        auto const& event = events[next];
                        auto pb = static_cast<PlayerButton>(event.button);
                        if (event.down) g_ghost->pushButton(pb);
                        else g_ghost->releaseButton(pb);
                        if (event.candidate) candidateApplied = true;
                        ++next;
                    }

                    if (step + 1 >= totalSteps) break;

                    const float beforeX = g_ghost->getPositionX();
                    g_ghost->update(dt);
                    const float movedX = g_ghost->getPositionX() - beforeX;

                    // Detached clones may update Y but not X. Fall back to the clone's current
                    // portal-aware velocity, then to the measured live-player X delta.
                    if (std::abs(movedX) < 0.0001f) {
                        const double vx = g_ghost->getCurrentXVelocity();
                        float fallbackDx = static_cast<float>(vx * dt);
                        if (!std::isfinite(fallbackDx) || std::abs(fallbackDx) < 0.0001f) {
                            fallbackDx = g_lastDx;
                        }
                        if (!std::isfinite(fallbackDx) || std::abs(fallbackDx) < 0.0001f) {
                            if (!g_warnedX) {
                                g_warnedX = true;
                                log::warn("[IB] clone X cannot be advanced reliably; fail closed");
                            }
                            out = unknown("ghost-x-static");
                            out.steps = step + 1;
                            break;
                        }
                        setCloneX(g_ghost, beforeX + fallbackDx);
                        if (std::abs(g_ghost->getPositionX() - beforeX) < 0.0001f) {
                            out = unknown("ghost-x-set-failed");
                            out.steps = step + 1;
                            break;
                        }
                        if (!g_warnedFallbackX) {
                            g_warnedFallbackX = true;
                            log::info("[IB] clone X fallback active (vx={} dt={} dx={})", vx, dt, fallbackDx);
                        }
                    }

                    g_ghost->updateRotation(dt);
                    g_ghost->updatePlayerScale();
                }
            }
        }

        if (g_ghost) {
            out.endX = g_ghost->getPositionX();
            out.endY = g_ghost->getPositionY();
            out.endYVelocity = static_cast<float>(g_ghost->m_yVelocity);
            out.endRotation = g_ghost->getRotation();
            out.endIsDart = g_ghost->m_isDart;
            out.endIsShip = g_ghost->m_isShip;
            out.endIsBall = g_ghost->m_isBall;
            out.endIsBird = g_ghost->m_isBird;
            out.endIsRobot = g_ghost->m_isRobot;
            out.endIsSpider = g_ghost->m_isSpider;
            out.endIsSwing = g_ghost->m_isSwing;
            out.endIsUpsideDown = g_ghost->m_isUpsideDown;
        }

        if (clearTargetX && !clearedTarget && out.result == SimResult::Safe) {
            out = unknown(budgetTruncated ? "clearance-budget-exhausted" : "hazard-clearance-timeout");
            out.steps = totalSteps;
        }

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
        if (!layer || !player) { ++g_skipOther; return 0; }
        if (limit <= 0)        { ++g_skipOff; return 0; }
        if (g_tick < 2)        { ++g_skipEarly; return 0; }
        if (player->m_isDead)  { ++g_skipDead; return 0; }
        if (player->m_isPlatformer || button != 1) {
            ++g_skipOther;
            return 0; // directional/platformer input isn't modeled by this predictor
        }
        if (!ensureGhost(layer)) { ++g_skipNoGhost; return 0; }

        const int key = buttonKey(button, true);
        auto keyIt = g_keys.find(key);
        if (keyIt != g_keys.end() && keyIt->second.pending > 0) {
            ++g_skipOther;
            return 0; // never predict a second press/release while the matching key is pending
        }

        const int baselineTick = g_tick + 1;
        const int immediateDue = effectiveDue(key, 0);
        if (immediateDue - baselineTick > limit) {
            ++g_skipOther;
            log::debug("[IB] {} tick {} -> NO-SEARCH (queue already beyond cap: +{} > {})",
                down ? "PRESS" : "RELEASE", g_tick, immediateDue - baselineTick, limit);
            return 0;
        }

        ++g_clicksPredicted;
        g_unknownReason = "";

        ActivationGuard guard(layer);
        int remainingSteps = kMaxStepsPerInput;
        auto runSimulation = [&](int due, std::optional<float> clearTarget = std::nullopt) -> SimOut {
            if (remainingSteps <= 0) return unknown("per-click-budget-exhausted");
            const SimOut result = simulate(layer, player, down, button, due, clearTarget,
                std::min(kMaxSimSteps, remainingSteps));
            remainingSteps = std::max(0, remainingSteps - std::max(1, result.steps));
            return result;
        };

        const SimOut base = runSimulation(immediateDue);
        guard.restore();

        int chosen = 0;
        int chosenDue = baselineTick;

        if (base.result == SimResult::Safe) {
            ++g_statKept;
            if constexpr (kLogVerdicts) {
                log::debug("[IB] {} tick {} -> NO-IMMEDIATE-DEATH (lookahead={} steps; {} budget steps left)",
                    down ? "PRESS" : "RELEASE", g_tick, base.steps, remainingSteps);
            }
        }
        else if (base.result == SimResult::Unknown) {
            ++g_statUnknown;
            if constexpr (kLogVerdicts) {
                log::debug("[IB] {} tick {} -> UNKNOWN ({})",
                    down ? "PRESS" : "RELEASE", g_tick, g_unknownReason);
            }
        }
        else {
            // A delayed event is only justified when the baseline death is repeatable and
            // tied to one stable physical object. Otherwise the target edge is untrustworthy.
            if (!base.knownPhysicalDeathObject || !base.deathObject ||
                !std::isfinite(base.deathObjectRight)) {
                ++g_statUnknown;
                g_unknownReason = "death-without-known-static-physical-object";
                log::debug("[IB] {} tick {} -> UNKNOWN ({})",
                    down ? "PRESS" : "RELEASE", g_tick, g_unknownReason);
                return 0;
            }

            const SimOut baselineConfirm = runSimulation(immediateDue);
            guard.restore();
            if (!sameBaselineDeath(base, baselineConfirm)) {
                ++g_statUnknown;
                g_unknownReason = "nondeterministic-baseline-death";
                log::warn("[IB] tick {} rejected: baseline death did not reproduce ({} vs {})",
                    g_tick, base.steps, baselineConfirm.steps);
                return 0;
            }

            const auto playerRect = player->GameObject::getObjectRect();
            const float halfPlayerWidth = std::max(
                kMinConservativeHalfWidth,
                std::abs(playerRect.size.width) * 0.5f
            );
            const float clearTargetX = base.deathObjectRight + halfPlayerWidth + kHazardClearancePadding;
            if (!std::isfinite(clearTargetX) ||
                clearTargetX <= player->getPositionX() + 0.1f ||
                clearTargetX <= base.deathX + 0.1f) {
                ++g_statUnknown;
                g_unknownReason = "invalid-hazard-clearance-target";
                log::debug("[IB] tick {} -> UNKNOWN ({}) targetX={:.2f} deathX={:.2f}",
                    g_tick, g_unknownReason, clearTargetX, base.deathX);
                return 0;
            }

            // Geometry moved by actions/triggers is not advanced by checkCollisions(clone).
            // If a physical moving/animated object exists in the predicted X corridor, do not
            // use the static-world simulation to authorize a delayed click.
            if (hasDynamicPhysicalObjectInXRange(
                    layer, player->getPositionX() - 32.0f, clearTargetX + 120.0f)) {
                ++g_statUnknown;
                g_unknownReason = "dynamic-physical-object-in-corridor";
                log::debug("[IB] tick {} -> UNKNOWN ({})", g_tick, g_unknownReason);
                return 0;
            }

            bool found = false;
            bool unstable = false;
            int unknownCands = 0;
            int bestSurvive = 0;
            int bestDelay = 0;
            int chosenClearSteps = 0;

            int lastDueChecked = immediateDue;
            for (int d = 1; d <= limit; ++d) {
                if (remainingSteps <= 0) {
                    g_unknownReason = "per-click-budget-exhausted";
                    unstable = true;
                    break;
                }
                const int due = effectiveDue(key, d);
                const int effectiveDelay = due - baselineTick;
                if (due == lastDueChecked) continue;
                lastDueChecked = due;
                if (effectiveDelay <= 0 || effectiveDelay > limit) continue;

                const SimOut r = runSimulation(due, clearTargetX);
                guard.restore();

                if (r.result == SimResult::Safe) {
                    // A positive verdict is replayed from a freshly seeded clone. The same
                    // clearance step and end-state must be reproduced before any click moves.
                    const SimOut confirm = runSimulation(due, clearTargetX);
                    guard.restore();
                    if (!sameSafeClearance(r, confirm)) {
                        ++unknownCands;
                        g_unknownReason = "nondeterministic-safe-candidate";
                        log::warn("[IB] tick {} delay {} rejected: clearance replay differs ({} vs {})",
                            g_tick, effectiveDelay, r.steps, confirm.steps);
                        unstable = true;
                        break;
                    }
                    chosen = d;
                    chosenDue = due;
                    chosenClearSteps = confirm.steps;
                    found = true;
                    break; // first stable clearance is the minimum delay
                }
                if (r.result == SimResult::Died && r.steps > bestSurvive) {
                    bestSurvive = r.steps;
                    bestDelay = d;
                }
                if (r.result == SimResult::Unknown) {
                    ++unknownCands;
                    // Unknown state (unsupported effect, moving geometry, X mismatch or budget
                    // exhaustion) invalidates this whole search; don't cherry-pick later trials.
                    unstable = true;
                    break;
                }
            }

            if (unstable) {
                ++g_statUnknown;
                if (!g_unknownReason || g_unknownReason[0] == '\0') g_unknownReason = "candidate-simulation-unknown";
                log::debug("[IB] {} tick {} -> UNKNOWN ({}, testedStepsLeft={})",
                    down ? "PRESS" : "RELEASE", g_tick, g_unknownReason, remainingSteps);
            }
            else if (found) {
                ++g_statSaved;
                const int actualDelay = chosenDue - baselineTick;
                const float nominalFrames = actualDelay * 0.25f;
                ac_console::line(fmt::format(
                    "[InputBuffer] *** SAVED +{} TICKS ({:.2f} nominal frames @ 60fps) {}! "
                    "(base death step={} object-right={:.2f} -> validated through step={}) [Limit: {}] ***",
                    actualDelay, nominalFrames, down ? "PRESS" : "RELEASE",
                    base.steps, base.deathObjectRight, chosenClearSteps, limit
                ));
                log::info(
                    "[IB] SAVED validated-twice event={} tick={} delay={} limit={} targetX={:.2f} "
                    "validatedThroughStep={} remainingBudget={}",
                    down ? "PRESS" : "RELEASE", g_tick, actualDelay, limit, clearTargetX,
                    chosenClearSteps, remainingSteps
                );
            }
            else {
                ++g_statNoSolution;
                if constexpr (kLogVerdicts) {
                    log::debug(
                        "[IB] {} tick {} -> NO SAFE CLEARANCE in {} ticks "
                        "(baseDeathStep={}, objectRight={:.2f}, bestDelay={}, surviveSteps={}, "
                        "unknownCandidates={}, remainingBudget={})",
                        down ? "PRESS" : "RELEASE", g_tick, limit, base.steps,
                        base.deathObjectRight, bestDelay, bestSurvive, unknownCands, remainingSteps
                    );
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
            if (key == cocos2d::enumKeyCodes::KEY_F6 && PlayLayer::get()) {
                ac::g_stage = (ac::g_stage + 1) % ac::kStageCount;
                ac_console::line("[InputBuffer] F6 Cycle -> " + ac::stageText(ac::g_stage));
            }
            else if (key == cocos2d::enumKeyCodes::KEY_F7 && PlayLayer::get()) {
                const auto status = ac::statusLine("STATUS");
                ac_console::line(status);
                log::info("{}", status);
            }
        }

        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
    }
};

class $modify(IBPlayLayer, PlayLayer) {
    void setupHasCompleted() {
        PlayLayer::setupHasCompleted();

        ac::showBannerOnce();
        ac::g_layer = this;
        ac::clearState();
        ac::clearStats();

        // ensureGhost cleans up the retained clone from any previous PlayLayer.
        ac::ensureGhost(this);
    }

    void resetLevel() {
        // Resumen automatico en cada reinicio/muerte (no hace falta pulsar F7).
        if (ac::g_clicksSeen > 0 || ac::g_tick > 0) {
            log::info("{}", ac::statusLine("ATTEMPT"));
        }
        ac::clearState();
        PlayLayer::resetLevel();
        ac::clearState();
        ac::clearStats(); // Keep ATTEMPT diagnostics scoped to one attempt.
    }

    // onExit tambien se dispara al pausar: no se destruye nada.
    void onExit() {
        ac::clearInputs();
        PlayLayer::onExit();
    }

    void onQuit() {
        if (ac::g_clicksSeen > 0 || ac::g_tick > 0) {
            log::info("{}", ac::statusLine("QUIT"));
        }
        ac::clearState();
        ac::destroyGhost();
        ac::g_layer = nullptr;
        PlayLayer::onQuit();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (ac::g_simulating && player == ac::g_ghost) {
            ac::g_cloneDied = true;
            ac::g_cloneDeathX = player ? player->getPositionX() : 0.0f;
            ac::g_cloneDeathObjectKnown = false;
            ac::g_cloneDeathObjectRight = 0.0f;
            ac::g_cloneDeathObject = nullptr;
            if (object && ac::isClearancePhysical(object) && !ac::hasDynamicPhysicalMotion(object)) {
                const auto rect = object->getObjectRect();
                const float right = rect.getMaxX();
                if (std::isfinite(right)) {
                    ac::g_cloneDeathObjectRight = right;
                    ac::g_cloneDeathObjectKnown = true;
                    ac::g_cloneDeathObject = object;
                }
            }
            return;
        }
        PlayLayer::destroyPlayer(player, object);
    }

    void playEndAnimationToPos(cocos2d::CCPoint p0) {
        if (!ac::g_simulating) PlayLayer::playEndAnimationToPos(p0);
    }
};

class $modify(IBBaseLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        if (ac::g_applyingQueuedInput || ac::g_simulating) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        auto* layer = PlayLayer::get();

        if (!layer ||
            static_cast<GJBaseGameLayer*>(layer) != static_cast<GJBaseGameLayer*>(this) ||
            !isPlayer1) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }

        ++ac::g_clicksSeen;
        if (ac::g_clicksSeen <= 3) {
            log::info(
                "[IB] handleButton #{} down={} button={} stage={} tick={} dtStable={} ghost={} dart={}",
                ac::g_clicksSeen, down, button, ac::g_stage + 1, ac::g_tick, ac::g_dtStable,
                ac::g_ghost ? "ok" : "NULL", layer->m_player1 ? layer->m_player1->m_isDart : false
            );
        }

        int delay = 0;
        auto* player = layer->m_player1;
        if (player && ac::kStages[ac::g_stage].all > 0) {
            delay = ac::predictDelay(layer, player, down, button);
        } else {
            ++ac::g_skipOff;
        }

        ac::queueInput(layer, down, button, isPlayer1, delay);
    }

    // xdBot: durante la simulacion se desactivan los objetos que no se modelan SOLO durante
    // esta llamada. Los que realmente tocan al fantasma marcan la simulacion como Unknown.
    void collisionCheckObjects(PlayerObject* p0, gd::vector<GameObject*>* objects, int p2, float p3) {
        if (!ac::g_simulating || p0 != ac::g_ghost || !objects) {
            GJBaseGameLayer::collisionCheckObjects(p0, objects, p2, p3);
            return;
        }

        std::vector<GameObject*> disabled;
        const cocos2d::CCRect pr = p0->GameObject::getObjectRect();
        constexpr float kMargin = 8.0f;
        const cocos2d::CCRect probe(
            pr.origin.x - kMargin, pr.origin.y - kMargin,
            pr.size.width + kMargin * 2.0f, pr.size.height + kMargin * 2.0f
        );

        for (auto* obj : *objects) {
            if (!obj) continue;
            if (obj->m_isDisabled || obj->m_isDisabled2) continue;

            const bool nearby = probe.intersectsRect(obj->getObjectRect());
            const bool modeled = ac::isModeled(obj);
            const bool harmless = ac::isHarmless(obj);

            if (nearby && !harmless &&
                (!modeled || ac::hasDynamicPhysicalMotion(obj))) {
                ac::g_unmodeled = true;
            }

            if (modeled) continue;
            disabled.push_back(obj);
            obj->m_isDisabled = true;
            obj->m_isDisabled2 = true;
        }

        GJBaseGameLayer::collisionCheckObjects(p0, objects, p2, p3);

        for (auto* obj : disabled) {
            // Objects were only added if both flags were false, so false is the exact prior state.
            obj->m_isDisabled = false;
            obj->m_isDisabled2 = false;
        }
    }

    bool canBeActivatedByPlayer(PlayerObject* p0, EffectGameObject* p1) {
        if (ac::g_simulating) {
            if (p0 == ac::g_ghost && p1 && ac::kModeledPortals.contains(p1->m_objectID)) {
                ac::handlePortal(p0, p1->m_objectID);
            } else {
                // Gravity/mode portals, pads and other effects are not modeled here.
                ac::g_unmodeled = true;
            }
            return false; // never let the clone activate a real level object
        }
        return GJBaseGameLayer::canBeActivatedByPlayer(p0, p1);
    }

    void playerTouchedRing(PlayerObject* p0, RingObject* p1) {
        if (ac::g_simulating) {
            if (p0 == ac::g_ghost) ac::g_unmodeled = true;
            return;
        }
        GJBaseGameLayer::playerTouchedRing(p0, p1);
    }

    void playerTouchedTrigger(PlayerObject* p0, EffectGameObject* p1) {
        if (ac::g_simulating) {
            if (p0 == ac::g_ghost && p1 && ac::kModeledPortals.contains(p1->m_objectID)) {
                ac::handlePortal(p0, p1->m_objectID);
            } else {
                ac::g_unmodeled = true;
            }
            return;
        }
        GJBaseGameLayer::playerTouchedTrigger(p0, p1);
    }

    void activateSFXTrigger(SFXTriggerGameObject* p0) {
        if (!ac::g_simulating) GJBaseGameLayer::activateSFXTrigger(p0);
    }

    void activateSongEditTrigger(SongTriggerGameObject* p0) {
        if (!ac::g_simulating) GJBaseGameLayer::activateSongEditTrigger(p0);
    }

    void gameEventTriggered(GJGameEvent p0, int p1, int p2) {
        if (!ac::g_simulating) GJBaseGameLayer::gameEventTriggered(p0, p1, p2);
    }
};

class $modify(IBPlayerObject, PlayerObject) {
    void update(float dt) {
        auto* layer = PlayLayer::get();
        const bool real = layer && this == layer->m_player1 && !ac::g_simulating;

        const bool advancingTick = real && std::isfinite(dt) && dt > 0.0f;
        if (advancingTick) {
            ac::dispatchPending(layer, ac::g_tick + 1);
        }

        const float beforeX = getPositionX();
        PlayerObject::update(dt);

        if (advancingTick) {
            const float dtTolerance = std::max(0.0001f, std::abs(ac::g_stepDt) * 0.01f);
            if (std::abs(dt - ac::g_stepDt) <= dtTolerance) ++ac::g_dtStable;
            else ac::g_dtStable = 0;

            ac::g_stepDt = dt;
            ac::g_lastDx = getPositionX() - beforeX;
            ++ac::g_tick;

            if (!ac::g_loggedFirstUpdate) {
                ac::g_loggedFirstUpdate = true;
                log::info("[IB] first real update dt={} dx={}", dt, ac::g_lastDx);
            }
        }
    }

    void playSpiderDashEffect(cocos2d::CCPoint p0, cocos2d::CCPoint p1) {
        if (!ac::g_simulating) PlayerObject::playSpiderDashEffect(p0, p1);
    }

    void incrementJumps() {
        if (!ac::g_simulating) PlayerObject::incrementJumps();
    }

    void ringJump(RingObject* p0, bool p1) {
        if (!ac::g_simulating) PlayerObject::ringJump(p0, p1);
    }
};

class $modify(IBHardStreak, HardStreak) {
    void addPoint(cocos2d::CCPoint p0) {
        if (!ac::g_simulating) HardStreak::addPoint(p0);
    }
};

class $modify(IBGameObject, GameObject) {
    void playShineEffect() {
        if (!ac::g_simulating) GameObject::playShineEffect();
    }
};

class $modify(IBEffectObject, EffectGameObject) {
    void triggerObject(GJBaseGameLayer* layer, int id, gd::vector<int> const* groups) {
        if (ac::g_simulating) {
            ac::g_unmodeled = true; // un trigger cambiaria el nivel: no se modela
            return;
        }
        EffectGameObject::triggerObject(layer, id, groups);
    }
};
