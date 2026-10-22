// Assisted Click - src/main.cpp -- v1.6.4 (anti-envenamiento del gate; trayectoria de SAVED arreglada; tolerancia estricta)
//
// Que hace: al recibir un clic (PRESS/RELEASE) simula un jugador nativo clonado en pasos
// medidos de PlayerObject::update, usando PlayLayer::checkCollisions del propio juego. Solo
// busca retrasos tras una muerte de referencia repetible contra geometria fisica estatica.
// No modela fase subtick de CBF/Click Between Steps; esas herramientas deben desactivarse.
//
// CAMBIOS v1.5.0 (arreglo del bloqueo masivo "unsupported-effect-object-in-corridor"):
//  1. El veto de triggers en el corredor ya no rechaza TODO EffectGameObject en el rango X.
//     Ahora: (a) los triggers detras del jugador no cuentan (X siempre crece en modo clasico),
//     (b) los triggers puramente visuales/audio estan en whitelist (color/pulse/alpha/shake/
//     animate/hide-show player, SFX/song) porque no pueden alterar la fisica ni via cadenas de
//     spawn, y (c) solo cuentan los triggers dentro de la banda Y alcanzable por las
//     trayectorias simuladas (banda exacta = union de los minY/maxY de todas las sims).
//  2. Veto temprano con banda conservadora (baseline + excursion maxima de candidato y
//     estres de release) para no gastar el presupuesto de busqueda cuando hay un trigger
//     fisico evidente; veto exacto post-candidato antes de autorizar el SAVED.
//  3. SimOut ahora registra minY/maxY de la trayectoria del clon.
//  4. Diagnostico: se registra el objectID/tipo/posicion del objeto que veta (corredor,
//     unmodeled) para saber exactamente que bloqueo cada veredicto.
//
// CAMBIOS v1.6.0 (arreglo del log 19.00.13: 0 SAVED en 69 clics):
//  1. PORTALES DE FORMA modelados en el clon: cube 12, ship 13, ball 47, ufo 111, wave 660,
//     robot 745, spider 1331, swing 1933. Antes el portal de wave (660) vetaba el corredor y
//     marcaba la sim como unmodeled: cualquier startpos ANTES de un portal de wave bloqueaba
//     el 100% de las predicciones (vetoedObjectID=660 en el log). Ahora handlePortal cambia
//     los flags m_isShip/m_isBird/m_isBall/m_isDart/m_isRobot/m_isSpider/m_isSwing del clon
//     (la fisica del modo la ejecuta el PlayerObject nativo en update), el seedGhost ya copia
//     y restaura esos flags en cada prediccion, y el portal queda exento del veto de triggers.
//     Los portales de GRAVEDAD (10/11/2926) siguen vetando: no se modela el flip.
//  2. Calibracion de edges con 1 muestra (antes 2). El log mostro intentos que mueren
//     manteniendo el click: sus releases reales casi nunca llegan mientras el jugador esta
//     vivo, asi que el perfil tardaba ~10 intentos en calibrar (48/86 UNKNOWN eran
//     "future-edge-history-not-calibrated"). Con 1 muestra la prediccion arranca un ciclo
//     completo antes. No hace falta una rama extra de robustez: el sim del candidato ya corre
//     sin edge opuesto sintetico (hold/gap mantenido hasta el horizonte) y las ramas
//     observadas cubren el edge opuesto en el instante medido; entre ambos cubren el
//     espacio completo de futuros del edge opuesto.
//  3. Los edges duplicados que GD re-envia al inicio de cada intento (re-sync del estado del
//     boton) ya no gastan predictDelay ni spamean UNKNOWN: heredan el delay activo y se
//     encolan tal cual. La deteccion es la misma del perfil de edges (observeRawInputEdge):
//     si el estado crudo del boton no cambia, no es un edge real.
//
// CAMBIOS v1.6.1 (arreglo del log 20.22.21: 36 muertes fantasma del clon en el spawn):
//  El log muestra 36 baselines que mueren en el PASO 1 contra un objeto con
//  objectRight=3.00, siempre en tick 0 (primer clic tras reaparecer), con todos los
//  retrasos candidatos muriendo tambien en el paso 1 (bestDelay=1, surviveSteps=1),
//  mientras el jugador real vive ~160 ticks. La simulacion a mitad de nivel funciona
//  (tick 30/52 -> NO-IMMEDIATE-DEATH), asi que el problema esta en el estado de
//  arranque del clon, no en el buscador. Mientras no se identifique el objeto exacto,
//  el mod deja de gastar presupuesto buscando retrasos contra una muerte que no
//  existe en la realidad:
//  1. DIAGNOSTICO: en cada muerte del clon con el intento recien arrancado (g_tick<=1)
//     se registra el rect del clon, el rect del jugador real y el rect/id/tipo del
//     objeto que mata. Con un log se identifica si es (a) hitbox distinta del clon,
//     (b) estado de spawn sin asentar, o (c) objeto que el juego real no mata.
//  2. GUARD DE MUERTE FANTASMA: tras un baseline que muere, se programa
//     g_physicsSerialTick + pasos + 3; si ese serialTick llega con el jugador real
//     vivo y sin edges nuevos de input, el baseline era falso: se registra
//     PHANTOM-DEATH y las predicciones quedan en pausa (fail open, input crudo) hasta
//     que un baseline vuelva a coincidir con la realidad (NO-IMMEDIATE-DEATH) o el
//     jugador muera de verdad dentro de la ventana prevista.
//  3. GUARD DE SPAWN: si el baseline muere en <=2 pasos con g_tick<=1, se devuelve
//     UNKNOWN("baseline-death-at-spawn-state") sin buscar: el estado de spawn del
//     clon no es de confianza. Esto elimina los 36 falsos NO-SOLUTION.
//  4. El veredicto UNKNOWN por objeto unmodeled ahora pega el ID/tipo del objeto en
//     la misma linea (antes habia que buscar la linea "sim step" anterior).
//  Siguen fail-closed a proposito: pads (35/67/140/1332), orbes y teletransporte
//  (747): son interacciones fisicas que la simulacion estatica no puede certificar.
//
// CAMBIOS v1.6.2 (el guard de v1.6.1 tapaba la causa; ahora valida el modelo contra la realidad):
//  v1.6.1 detectaba la muerte fantasma y se negaba a actuar, pero (1) bloqueaba tambien
//  muertes REALES en spawn (startpos pegada a un pico), (2) la pausa era global y pegajosa
//  (bloqueaba saves reales a mitad de intento), (3) un SAVED sobre un baseline falso nunca
//  se verificaba, (4) los gates g_tick<=1 eran demasiado estrechos y (5) STAGE 1 (limite 2)
//  hace casi imposible un SAVED en wave. v1.6.2 reemplaza el guard de muerte fantasma por
//  un CHEQUEO DE REALIDAD que valida TODO el modelo, no solo las muertes:
//  1. TRAYECTORIA: cada baseline Safe (KEPT) y cada SAVED arma un chequeo con la posicion
//     final del clon (endX/endY/steps); cuando el serialTick llega, la posicion del jugador
//     real debe coincidir (+-kRealityPositionTolerance). Si difiere (o muere), el modelo
//     diverge: MODEL-DIVERGENCE, pausa y log con posiciones esperadas vs reales. Esto
//     detecta fantasmas en tick 2/3, SAVED falsos y falsos NO-IMMEDIATE-DEATH.
//  2. MUERTE PREVISTA: los baselines que mueren siguen programando su muerte esperada;
//     si el jugador real muere dentro de la ventana, el baseline acerto (se libera la
//     pausa y, si steps<=2, g_spawnDeathsReal=true DESACTIVA el guard de spawn: una
//     startpos pegada a un pico deja de dar UNKNOWN en cada intento). Si el jugador
//     sigue vivo: PHANTOM-DEATH confirmada, pausa.
//  3. PAUSA ACOTADA: la pausa solo bloquea clics con g_tick<=3 (ventana de spawn). Los
//     clics a mitad de intento buscan normalmente aunque el modelo este pausado, y si
//     su trayectoria coincide con la realidad, la pausa se libera.
//  4. GATES AMPLIADOS: el dump CLONE-DEATH y el guard de spawn cubren g_tick<=3, no solo
//     tick 0/1.
//  5. STAGE INICIAL 3 (8/8): el limite 2 de STAGE 1 casi nunca deja ventana de 3 clears
//     en wave. F6 sigue ciclando 2/2 -> 4/4 -> 8/8 -> 16/8 -> OFF.
//  6. BANDA Y DEL VETO TEMPRANO: en wave la excursion vertical por tick es |dx| (~1.3),
//     no el suelo 3.5 de los otros modos; con el suelo se inflaba la banda y vetaba
//     triggers inalcanzables. Floor 96 -> 48 (el veto exacto post-candidato sigue de
//     respaldo fail-closed).
//
// CAMBIOS v1.6.3 (revision del chequeo de realidad: el v1.6.2 era solo observacional):
//  1. OFF-BY-ONE DEL DEADLINE: el clon hace steps-1 updates reales (el ultimo paso
//     solo chequea colision y sale antes del update), pero el deadline sumaba steps
//     completos: el jugador real se comparaba con un update de ventaja. En wave el
//     desfase (~1.3) cabia en la tolerancia, pero en cubo/ball cayendo supera 3 y daba
//     MODEL-DIVERGENCE falsos. Ahora el deadline es serial + max(1, steps - 1).
//  2. TRAYECTORIA POR PASO: casi ningun chequeo llegaba a resolverse en wave (cualquier
//     edge nuevo lo cancelaba, y en wave los edges llegan cada pocos ticks). Ahora
//     simulate() guarda la posicion del clon en CADA paso y, cuando llega el siguiente
//     edge real, se compara la posicion del jugador real contra el paso correspondiente:
//     el chequeo se resuelve (match o divergencia) aunque el edge llegue mucho antes
//     del deadline. La pausa tambien se libera con un match en el edge.
//  3. GATE DE SAVED: la validacion v1.6.2 era solo observacional: si el modelo divergia
//     a mitad de intento, el mod seguia autorizando retrasos. Ahora un SAVED exige
//     kMinTrajectoryMatchesForSave coincidencias de trayectoria consecutivas y que no
//     haya una divergencia en los ultimos kDivergenceSaveCooldownTicks serial ticks.
//     Mientras el modelo no este validado, los clics con baseline mortal devuelven
//     UNKNOWN("save-authorization-unvalidated-model") y pasan crudos (fail open).
//     Un baseline Safe (KEPT) sigue funcionando y acumula las coincidencias que
//     desbloquean los SAVED.
// CAMBIOS v1.6.4 (cuatro bugs del v1.6.3 que iban a seguir fallando):
//  1. ANTIVENENO DEL GATE: cada PHANTOM-DEATH al reaparecer (una por clic de spawn en
//     el escenario del log 20.22.21) ponia g_trajectoryMatches=0 y arrancaba el
//     cooldown de 120 ticks: con intentos de ~160 ticks el gate casi nunca se abria.
//     Ahora el chequeo recuerda si se armo dentro de la ventana de spawn
//     (armedInSpawnWindow, g_tick<=3) y un PHANTOM-DEATH de spawn NO envenena la
//     validacion acumulada: las muertes fantasma del estado de spawn son un problema
//     ya conocido y aislado, no evidencia contra el modelo a mitad de nivel.
//  2. DEADLOCK DEL ESCAPE DE SPAWN: una muerte real que coincide con la prevista no
//     sumaba evidencia. Con una startpos pegada a un pico el primer clic ya es el
//     mortal, nunca hay KEPT previo y el gate no se abria jamas. Ahora la muerte
//     confirmada (++g_trajectoryMatches) tambien valida el modelo.
//  3. TRAYECTORIA DEL SAVED VACIA: "out = {SimResult::Safe, step+1}" reasignaba TODO
//     el SimOut y borra out.traj: chosenTraj llegaba vacio y la validacion por edge
//     de los SAVED se descartaba sin evidencia. Ahora se asignan solo result/steps.
//  4. TOLERANCIA ESTRICTA: el clon ejecuta el mismo PlayerObject::update desde el
//     mismo estado, asi que la coincidencia deberia ser casi exacta; con 3.0 un error
//     de fase de un update entero en wave (~1.3) pasaba por match. Bajada a 0.5:
//     si el log muestra divergencias falsas, subir gradualmente.
//  5. IDENTIDAD DEL OBJETO DE MUERTE: m_objectID es el TIPO, no la instancia (dos
//     picos cualquiera "coincidian"). Ahora se compara el puntero del GameObject y
//     se guarda en RealityCheck.
//  6. BINDING: getCurrentXVelocity() (el que ya usa simulate()) en vez de m_xVelocity.
//  4. ESCAPE DE SPAWN VERIFICADO: g_spawnDeathsReal se podia activar por casualidad
//     (morir de verdad en los ~4 ticks siguientes por cualquier causa desactivaba el
//     guard para TODO el nivel). Ahora la muerte real debe coincidir con la prevista:
//     mismo objectID y deathX dentro de kRealityDeathXTolerance; si no, no hay escape.
//  5. BANDA Y TRAS RESET: g_lastDx es 0 justo despues de un reset y el floor 0.5 de
//     wave subestimaba la excursion. Fallback: |m_xVelocity| * g_stepDt (velocidad
//     nativa medida del jugador real).
//  6. Los UNKNOWN por pads/orbes/teletransporte (35/67/140/1332/747) siguen ahi A
//     PROPOSITO: fisica que la simulacion estatica no puede certificar (fail closed).
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
#include <chrono>
#include <cstdint>

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

    // ---- Etapas: ticks de actualización de PlayerObject, no sub-ticks de CBF.
    struct Stage { int all; int wave; };
    constexpr Stage kStages[] = { {2, 2}, {4, 4}, {8, 8}, {16, 8}, {0, 0} };
    constexpr int kStageCount = 5;

    // Ventana CRITICA: ticks que se simulan tras aplicar la entrada. La simulacion mantiene
    // Lookahead breve para decidir si tiene sentido buscar: no es el veredicto de seguridad.
    // La validacion posterior comprueba un corredor mayor con colisiones nativas y presupuesto.
    // Short baseline lookahead: this is only a trigger for searching, NOT proof that
    // a candidate is safe. Candidate validation runs until it clears the baseline hazard.
    constexpr int kHorizon = 16; // trigger for search only; never proof of safety
    constexpr int kMaxSimSteps = 144;   // allows a slow-speed wave to reach a nearby validation corridor
    constexpr int kMaxStepsPerInput = 2048; // aggregate simulated ticks
    constexpr int kMaxClearanceEstimate = 128; // max estimated physics updates to the validation corridor
    constexpr int kMaxEdgeProfileTicks = 512; // exact interval storage cap; longer intervals use a sentinel meaning no opposite edge in the modeled horizon
    constexpr int kPostClearanceSteps = 2; // collision samples after validation corridor
    constexpr float kPostClearanceDistance = 24.0f; // inspect nearby chained hazards without an excessive fixed tail
    constexpr float kHazardClearancePadding = 8.0f;
    constexpr float kTriggerBandYPad = 8.0f; // margen de la banda Y del veto exacto de triggers
    constexpr auto kPredictionWallBudget = std::chrono::milliseconds(6);
    constexpr int kEdgeProfileSamples = 4;
    // v1.6.0: antes 2. Con clics que mueren manteniendo el boton, el edge opuesto real llega
    // ~1 vez por intento; exigir 2 muestras dejaba el mod en UNKNOWN durante ~10 intentos.
    // Con 1 muestra: el sim del candidato corre sin edge opuesto (hold/gap mantenido) y la
    // rama observada cubre el edge opuesto en el instante medido: espacio completo cubierto.
    constexpr int kMinimumEdgeSamples = 1;
    constexpr float kMinConservativeHalfWidth = 20.0f; // covers normal-size and rotating hitboxes
    constexpr bool kLogVerdicts = true; // veredicto por clic -> log de Geode (NO consola)

    // v1.6.2: empieza en STAGE 3 (8/8). STAGE 1 (limite 2) casi nunca deja una ventana
    // de 3 clears consecutivos en wave, asi que todo clic daba NO SAFE CLEARANCE por
    // diseño, no por bug. F6 sigue ciclando todos los stages.
    int g_stage = 2; // empieza en STAGE 3

    std::string stageText(int i) {
        auto const& s = kStages[i];
        if (s.all == 0) {
            return fmt::format("STAGE {}: OFF (100% Raw Input)", i + 1);
        }
        if (s.all == s.wave) {
            return fmt::format(
                "STAGE {}: {} update ticks All | {} update ticks Wave",
                i + 1, s.all, s.wave
            );
        }
        return fmt::format(
            "STAGE {}: {} update ticks All | {} update ticks Wave",
            i + 1, s.all, s.wave
        );
    }

    void showBannerOnce() {
        static bool shown = false;
        if (shown || !ac_console::init()) return;
        shown = true;

        auto const& s = kStages[g_stage];
        ac_console::line("===================================================");
        ac_console::line("[InputBuffer] v1.6.4 LOADED (spawn-safe validation; strict trajectory tolerance)");
        ac_console::line(fmt::format(
            "[InputBuffer] Starting Mode: STAGE {} ({} All / {} Wave)", g_stage + 1, s.all, s.wave
        ));
        ac_console::line("[InputBuffer] F6: 2/2 -> 4/4 -> 8/8 -> 16/8 -> OFF");
        ac_console::line("[InputBuffer] Tick-based only; disable CBF / Click Between Steps / Click Between Ticks");
        ac_console::line("===================================================");
    }

    int limitFor(PlayerObject* p) {
        auto const& s = kStages[g_stage];
        return (p && p->m_isDart) ? s.wave : s.all;
    }

    // ---- Estado de la partida
    int g_tick = 0;           // updates reales del jugador 1 dentro del intento actual
    std::int64_t g_physicsSerialTick = 0; // monotonic across retries; input intervals must not use resettable g_tick
    float g_stepDt = 0.0f;    // mediana de dt medida, nunca asumida
    float g_lastDx = 0.0f;
    int g_dtStable = 0;
    std::deque<float> g_recentDt;
    constexpr size_t kDtCalibrationSamples = 8;
    constexpr float kDtRelativeRangeTolerance = 0.001f; // max 0.1% de rango en toda la ventana
    constexpr float kDtAbsoluteRangeTolerance = 0.000001f;

    PlayLayer* g_layer = nullptr;
    PlayerObject* g_ghost = nullptr;
    PlayLayer* g_ghostOwner = nullptr;

    bool g_simulating = false;
    bool g_cloneDied = false;
    bool g_unmodeled = false; // la simulacion toco algo que no sabe modelar
    bool g_applyingQueuedInput = false;

    // ---- Diagnostico (solo log de Geode)
    const char* g_unknownReason = "";
    // Identidad del primer objeto que marco la simulacion como no modelada (diagnostico).
    int g_unmodeledObjectID = -1;
    int g_unmodeledObjectType = -1;
    int g_clicksSeen = 0, g_clicksPredicted = 0;
    int g_skipOff = 0, g_skipEarly = 0, g_skipDead = 0, g_skipNoGhost = 0, g_skipOther = 0;
    int g_statKept = 0, g_statSaved = 0, g_statNoSolution = 0, g_statUnknown = 0;
    bool g_warnedX = false, g_warnedSeed = false, g_warnedWorld = false;
    bool g_warnedFallbackX = false, g_warnedDualMode = false, g_warnedDeathQueue = false;
    long long g_lastPredictionMicros = 0;

    // ---- v1.6.2/v1.6.3: chequeo de realidad (valida TODO el modelo contra el mundo real) ----
    // Dos modos:
    //  - expectDeath=true: el baseline predijo que el jugador real muere en ~steps pasos.
    //    Si llega el deadline vivo -> PHANTOM-DEATH (muerte fantasma del clon).
    //  - expectDeath=false: el baseline fue Safe (KEPT) o se autorizo un SAVED; el clon
    //    recorrio la trayectoria guardada (traj, posicion por paso). El jugador real debe
    //    seguirla: en el deadline (paso steps-1) o, si llega antes un edge nuevo, en el
    //    paso correspondiente al edge. Si difiere -> MODEL-DIVERGENCE.
    // v1.6.3: el deadline corrige el off-by-one (el clon hace steps-1 updates reales) y el
    // edge resuelve el chequeo contra el paso correspondiente en vez de cancelarlo a ciegas.
    constexpr float kRealityPositionTolerance = 0.5f; // v1.6.4: era 3.0; un desfase de un update entero pasaba por match
    // Tolerancia holgada para confirmar una MUERTE prevista: la fase subtick puede
    // desplazar el X de la muerte real en ~1 paso respecto del clon.
    constexpr float kRealityDeathXTolerance = 8.0f;
    // Gate de SAVED (v1.6.3): no se autoriza ningun retraso hasta que el modelo haya
    // demostrado N coincidencias de trayectoria seguidas, y nunca con una divergencia
    // reciente (cooldown en serial ticks).
    constexpr int kMinTrajectoryMatchesForSave = 2;
    constexpr int kDivergenceSaveCooldownTicks = 120;
    struct RealityCheck {
        bool active = false;
        bool expectDeath = false;
        int steps = 0;
        float expectX = 0.0f;
        float expectY = 0.0f;
        // Solo para expectDeath (v1.6.3): la muerte real debe coincidir con la prevista.
        float expectDeathX = 0.0f;
        int expectDeathObjectID = -1;
        // v1.6.4: identidad de INSTANCIA (m_objectID es el tipo, no el objeto).
        GameObject* expectDeathObject = nullptr;
        // v1.6.4: true si el chequeo se armo dentro de la ventana de spawn (g_tick<=3).
        // Un PHANTOM-DEATH de spawn no envenena la validacion acumulada del nivel.
        bool armedInSpawnWindow = false;
        bool down = false;
        int button = 1;
        std::int64_t armSerialTick = 0;
        std::int64_t deadlineSerialTick = 0;
        // Posicion del clon en cada paso (indice = pasos desde el arme). v1.6.3.
        std::vector<std::pair<float, float>> traj;
    };
    RealityCheck g_realityCheck;
    int g_phantomDeaths = 0;      // muertes fantasma confirmadas (baseline decia muerte, real vivo)
    int g_modelDivergences = 0;   // trayectorias que no coincidieron con la realidad
    bool g_predictionsPaused = false;
    // v1.6.2: escape del guard de spawn. Si una muerte del jugador real en <=2 pasos
    // llega dentro de la ventana prevista, la muerte en spawn era REAL (startpos pegada
    // a un pico): dejar de tratar el estado de spawn como no confiable. Por nivel.
    // v1.6.3: solo si la muerte real coincide con la prevista (mismo objeto y deathX).
    bool g_spawnDeathsReal = false;
    // v1.6.3: validacion del modelo acumulada en ESTE nivel. Coincidencias de
    // trayectoria consecutivas (cualquier divergencia las pone a 0) y serial tick de la
    // ultima divergencia. Un SAVED exige matches >= kMinTrajectoryMatchesForSave y
    // ninguna divergencia en los ultimos kDivergenceSaveCooldownTicks.
    int g_trajectoryMatches = 0;
    std::int64_t g_lastDivergenceSerial = -1000000;

    // v1.6.3: el modelo solo puede autorizar SAVEDs cuando ha demostrado coincidencias
    // de trayectoria y no ha divergido recientemente. Validacion acumulada POR NIVEL.
    bool saveAuthorizationValidated() {
        return g_trajectoryMatches >= kMinTrajectoryMatchesForSave &&
            (g_physicsSerialTick - g_lastDivergenceSerial) > kDivergenceSaveCooldownTicks;
    }

    // v1.6.3: resuelve un chequeo de trayectoria (!expectDeath) contra el paso en el que
    // llego el edge. En wave los edges llegan cada pocos ticks: casi ningun chequeo
    // llegaba al deadline. Ahora cada edge real valida el paso correspondiente y el
    // chequeo se resuelve con evidencia (match o divergencia) en vez de cancelarse.
    // Devuelve true si el chequeo estaba activo y se resolvio aqui.
    bool resolveRealityCheckAtEdge() {
        if (!g_realityCheck.active || g_realityCheck.expectDeath) return false;
        auto* player = g_layer ? g_layer->m_player1 : nullptr;
        if (!player || g_realityCheck.traj.empty()) return false;
        const auto sinceArm = g_physicsSerialTick - g_realityCheck.armSerialTick;
        if (sinceArm < 1) return false; // sin updates nuevos no hay evidencia
        const size_t idx = static_cast<size_t>(std::min<std::int64_t>(
            sinceArm, static_cast<std::int64_t>(g_realityCheck.traj.size()) - 1));
        const auto& [expX, expY] = g_realityCheck.traj[idx];
        const bool matches = !player->m_isDead &&
            std::abs(player->getPositionX() - expX) <= kRealityPositionTolerance &&
            std::abs(player->getPositionY() - expY) <= kRealityPositionTolerance;
        if (matches) {
            ++g_trajectoryMatches;
            if (g_predictionsPaused) {
                g_predictionsPaused = false;
                log::warn("[IB] PHANTOM-GUARD released at tick {} (edge match, step {}): "
                          "real=({:.2f},{:.2f}) expect=({:.2f},{:.2f})",
                    g_tick, idx, player->getPositionX(), player->getPositionY(), expX, expY);
            }
            else {
                log::debug("[IB] reality trajectory match at step {} (real=({:.2f},{:.2f}) "
                           "expect=({:.2f},{:.2f}); matches={})",
                    idx, player->getPositionX(), player->getPositionY(), expX, expY, g_trajectoryMatches);
            }
        }
        else {
            g_trajectoryMatches = 0;
            g_lastDivergenceSerial = g_physicsSerialTick;
            ++g_modelDivergences;
            g_predictionsPaused = true;
            log::warn("[IB] MODEL-DIVERGENCE at edge (step {}): real=({:.2f},{:.2f}) dead={} "
                      "but model expected ({:.2f},{:.2f}); matches reset, predictions paused",
                idx, player->getPositionX(), player->getPositionY(),
                player->m_isDead ? 1 : 0, expX, expY);
        }
        return true;
    }

    bool g_loggedFirstUpdate = false;

    std::string statusLine(const char* tag) {
        return fmt::format(
            "[IB] {} stage={} ghost={} tick={} dtStable={} clicks={} predicted={} kept={} saved={} "
            "nosol={} unknown={} lastUnknown={} predictUs={} | skipped: off={} early={} dead={} noGhost={} other={} "
            "| phantom: deaths={} divergences={} paused={} spawnReal={} matches={}",
            tag, g_stage + 1, g_ghost ? "ok" : "NULL", g_tick, g_dtStable, g_clicksSeen,
            g_clicksPredicted, g_statKept, g_statSaved, g_statNoSolution, g_statUnknown,
            g_unknownReason, g_lastPredictionMicros, g_skipOff, g_skipEarly, g_skipDead, g_skipNoGhost, g_skipOther,
            g_phantomDeaths, g_modelDivergences, g_predictionsPaused ? 1 : 0, g_spawnDeathsReal ? 1 : 0,
            g_trajectoryMatches
        );
    }

    enum class SimResult { Safe, Died, Unknown };

    struct SimPerturbation {
        float dtScale = 1.0f;
        float startYOffset = 0.0f;
        float yVelocityOffset = 0.0f;
    };

    struct SimOut {
        SimResult result;
        int steps;
        float deathX = 0.0f;
        float deathObjectRight = 0.0f;
        bool knownPhysicalDeathObject = false;
        GameObject* deathObject = nullptr;
        float endX = 0.0f;
        float endY = 0.0f;
        // v1.6.3: posicion del clon en cada paso (indice = paso; traj[0] = posicion
        // inicial). Usada por el chequeo de realidad para comparar el paso exacto en el
        // que llega un edge nuevo. Solo se llena en simulaciones que completan pasos.
        std::vector<std::pair<float, float>> traj;
        // Recorrido vertical del clon durante la simulacion (para la banda Y del corredor).
        float minY = 0.0f;
        float maxY = 0.0f;
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
        bool endIsOnGround = false;
        bool endJumpBuffered = false;
        bool endInputsLocked = false;
        bool endControlsDisabled = false;
        bool endStateFlipGravity = false;
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
    // Delay selected for the active press of each button. A release inherits this offset,
    // so a delayed press/release pair does not collapse to a one-tick hold.
    std::unordered_map<int, int> g_activePressDelay;

    // Recent real input intervals only; synthetic validation edges never train this profile.
    // Timestamps use g_physicsSerialTick, because g_tick is reset on every retry.
    struct EdgeProfile {
        bool initialized = false;
        bool lastDown = false;
        std::int64_t pressStartTick = -1;
        std::int64_t releaseTick = -1;
        std::deque<int> holdTicks;
        std::deque<int> releaseGapTicks;
    };
    std::unordered_map<int, EdgeProfile> g_edgeProfiles;
    using SteadyClock = std::chrono::steady_clock;

    int buttonKey(int button, bool isPlayer1) {
        return button + (isPlayer1 ? 0 : 10000);
    }

    // Keep long intervals as evidence instead of discarding them. A hold/gap longer than
    // kMaxEdgeProfileTicks cannot be represented as an exact future edge in our short native
    // simulation window, but it is useful evidence that no opposite edge occurred inside that
    // window. Store a saturating sentinel (513) so it counts toward calibration and maps to 0
    // in futureEdgeOffsets().
    void pushBounded(std::deque<int>& values, std::int64_t value) {
        if (value <= 0) return;
        const int stored = value > kMaxEdgeProfileTicks
            ? kMaxEdgeProfileTicks + 1
            : static_cast<int>(value);
        values.push_back(stored);
        while (values.size() > kEdgeProfileSamples) values.pop_front();
    }

    // Devuelve true si este edge cambia el estado crudo del boton (edge real para el perfil)
    // y false si es un duplicado: GD re-envia el estado del boton al inicio de cada intento
    // (re-sync tras muerte/retry), y ese edge duplicado no es una muestra de timing real.
    bool observeRawInputEdge(int key, bool down) {
        auto& profile = g_edgeProfiles[key];
        if (!profile.initialized) {
            profile.initialized = true;
            profile.lastDown = down;
            profile.pressStartTick = down ? g_physicsSerialTick : -1;
            profile.releaseTick = down ? -1 : g_physicsSerialTick;
            return true;
        }
        if (profile.lastDown == down) return false; // duplicate edge, not a timing sample
        // v1.6.3: un edge real cambia el futuro del input. Un chequeo de MUERTE prevista
        // ya no es comparable y se cancela. Un chequeo de TRAYECTORIA no se descarta:
        // se resuelve contra el paso correspondiente (en wave los edges llegan cada
        // pocos ticks y casi ningun chequeo llegaba al deadline) y luego se desactiva.
        if (g_realityCheck.active) {
            resolveRealityCheckAtEdge(); // valida el paso del edge; el futuro ya cambio
        }
        g_realityCheck.active = false;
        if (down) {
            if (profile.releaseTick >= 0) {
                const auto gap = g_physicsSerialTick - profile.releaseTick;
                pushBounded(profile.releaseGapTicks, gap);
            }
            profile.pressStartTick = g_physicsSerialTick;
            profile.lastDown = true;
        } else {
            if (profile.pressStartTick >= 0) {
                const auto hold = g_physicsSerialTick - profile.pressStartTick;
                pushBounded(profile.holdTicks, hold);
            }
            profile.releaseTick = g_physicsSerialTick;
            profile.lastDown = false;
        }
        return true;
    }

    void resetEdgeProfileClocks(bool discardSamples) {
        if (discardSamples) {
            g_edgeProfiles.clear();
            return;
        }
        // A death/retry resets g_tick, not the physical input history. Keep the last raw
        // button state and its monotonic timestamps so a release delivered just after reset
        // still closes the hold interval from the previous attempt. Rebasing here made
        // holdTicks/releaseGapTicks permanently empty for inputs that caused a death.
    }

    std::vector<int> futureEdgeOffsets(int key, bool currentDown, int maxCandidateDelay) {
        auto it = g_edgeProfiles.find(key);
        if (it == g_edgeProfiles.end()) return {};
        auto const& samples = currentDown ? it->second.holdTicks : it->second.releaseGapTicks;
        if (samples.size() < kMinimumEdgeSamples) return {};
        std::vector<int> offsets;
        // A learned opposite edge that happens after the largest trajectory we can certify
        // is represented by 0 (no opposite edge inside that simulated horizon). Otherwise the
        // timestamp would force simulate() to extend past its strict step cap and always return
        // UNKNOWN, even when the event is observed much later than the hazard corridor.
        const int maxRepresentableOffset = std::max(1,
            kMaxSimSteps - kHorizon - std::max(0, maxCandidateDelay) - 1);
        for (int n : samples) {
            if (n <= 0) continue;
            // 0 means the observed opposite edge was later than the trajectory can model, so
            // no synthetic opposite edge is inserted before the clearance horizon. This is not
            // an invented duration: it is a bucket for an observed interval outside the window.
            const int modeled = (n > maxRepresentableOffset) ? 0 : n;
            if (std::find(offsets.begin(), offsets.end(), modeled) == offsets.end()) offsets.push_back(modeled);
        }
        // v1.6.0 (nota de diseño): con 1 sola muestra no hace falta anadir una rama extra
        // "sin edge opuesto": el sim del candidato YA corre sin edge sintetico (hold/gap
        // mantenido hasta el horizonte), y las ramas observadas cubren el edge opuesto en el
        // instante medido. Entre ambos cubren el espacio completo de futures del edge opuesto.
        return offsets;
    }

    struct SimulationScope {
        bool prev;
        SimulationScope() : prev(g_simulating) {
            g_simulating = true;
            g_cloneDied = false;
            g_unmodeled = false;
            g_unmodeledObjectID = -1;
            g_unmodeledObjectType = -1;
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
        g_activePressDelay.clear();
    }

    // Calibrate against the full recent window; reject clock drift rather than substituting
    // nominal physics. The strict tolerance is intentional until live dt measurements justify it.
    void observePhysicsDt(float dt) {
        if (!std::isfinite(dt) || dt <= 0.0f) {
            g_recentDt.clear();
            g_dtStable = 0;
            g_stepDt = 0.0f;
            return;
        }
        g_recentDt.push_back(dt);
        while (g_recentDt.size() > kDtCalibrationSamples) g_recentDt.pop_front();
        g_stepDt = dt;
        g_dtStable = 0;
        if (g_recentDt.size() < kDtCalibrationSamples) return;

        std::vector<float> ordered(g_recentDt.begin(), g_recentDt.end());
        std::sort(ordered.begin(), ordered.end());
        const float median = (ordered[3] + ordered[4]) * 0.5f;
        const float range = ordered.back() - ordered.front();
        const float tolerance = std::max(kDtAbsoluteRangeTolerance,
            std::abs(median) * kDtRelativeRangeTolerance);
        if (!std::isfinite(median) || median <= 0.0f || range > tolerance) {
            // Keep only this sample and wait for a new complete stable window.
            g_recentDt.clear();
            g_recentDt.push_back(dt);
            g_stepDt = dt;
            return;
        }
        g_stepDt = median;
        g_dtStable = static_cast<int>(g_recentDt.size());
    }

    void clearState(bool retainInputTimingProfile = false, bool retainPhysicsCalibration = false) {
        clearInputs();
        resetEdgeProfileClocks(!retainInputTimingProfile);
        if (!retainInputTimingProfile) g_physicsSerialTick = 0;
        g_tick = 0;
        g_lastDx = 0.0f;

        // A retry resets the player/tick state, but not the physics clock itself. Keep a
        // fully measured, stable dt window across retries of the SAME PlayLayer. The next
        // real updates immediately replace the oldest samples; if timing changed, the normal
        // full-window range check invalidates calibration before later predictions can run.
        // New PlayLayer / quit paths pass retainPhysicsCalibration=false.
        if (!retainPhysicsCalibration) {
            g_stepDt = 0.0f;
            g_dtStable = 0;
            g_recentDt.clear();
        }

        g_cloneDied = false;
        g_unmodeled = false;
        g_applyingQueuedInput = false;
        // v1.6.2: el intento termino; un chequeo de realidad pendiente ya no
        // puede resolverse contra este intento. La pausa NO se toca: describe la
        // confianza del modelo en el estado de spawn, que se repite en cada intento.
        g_realityCheck.active = false;
    }

    void clearStats() {
        g_unknownReason = "";
        g_clicksSeen = g_clicksPredicted = 0;
        g_skipOff = g_skipEarly = g_skipDead = g_skipNoGhost = g_skipOther = 0;
        g_loggedFirstUpdate = false;
        g_statKept = g_statSaved = g_statNoSolution = g_statUnknown = 0;
        g_warnedX = g_warnedSeed = g_warnedWorld = false;
        g_warnedFallbackX = g_warnedDualMode = g_warnedDeathQueue = false;
        g_lastPredictionMicros = 0;
        g_phantomDeaths = 0;
        g_modelDivergences = 0;
    }

    // ========================================================================
    // Objetos modelados / no modelados (misma idea que xdBot: objectTypes + portalIDs)
    // ========================================================================
    // Portales que SI se modelan (tamano, velocidad y FORMA v1.6.0), igual que
    // ShowTrajectory::handlePortal. La forma se aplica al clon cambiando los flags m_is*
    // en handlePortal(); la fisica del modo la calcula el PlayerObject nativo en update().
    // El portal de gravedad (10/11/2926) NO esta: el flip no se modela (fail closed).
    const std::unordered_set<int> kModeledPortals = {
        12, 13, 47, 111, 660, 745, 1331, 1933, // forma: cube, ship, ball, ufo, wave, robot, spider, swing
        101, 99,                                // mini / big
        200, 201, 202, 203, 1334                // velocidad
    };

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


    bool rectsOverlap(cocos2d::CCRect const& a, cocos2d::CCRect const& b) {
        return a.getMinX() <= b.getMaxX() && a.getMaxX() >= b.getMinX() &&
               a.getMinY() <= b.getMaxY() && a.getMaxY() >= b.getMinY();
    }

    // If destroyPlayer's object argument is null or unusable, search the current level's
    // authoritative object array for static physical objects whose bounding boxes overlap the
    // ghost's final hitbox. This is broad-phase evidence, not proof of exact shape contact, so
    // choose the rightmost overlapping candidate (conservative clearance) and still require the
    // full later trajectory validation. Never fabricate a target from deathX alone.
    GameObject* findStaticDeathColliderFromObjects(PlayLayer* layer, PlayerObject* player) {
        if (!layer || !layer->m_objects || !player) return nullptr;
        const auto playerRect = player->GameObject::getObjectRect();
        GameObject* best = nullptr;
        float bestRight = -std::numeric_limits<float>::infinity();
        for (auto* object : CCArrayExt<GameObject*>(layer->m_objects)) {
            if (!object || object->m_isDisabled || object->m_isDisabled2 ||
                !isClearancePhysical(object) || hasDynamicPhysicalMotion(object)) continue;
            const auto objectRect = object->getObjectRect();
            if (!rectsOverlap(playerRect, objectRect)) continue;
            const float right = objectRect.getMaxX();
            if (std::isfinite(right) && right > bestRight) {
                best = object;
                bestRight = right;
            }
        }
        return best;
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

    // ---- Politica de triggers en el corredor (v1.5.0) ----
    // Triggers que solo afectan visuales o audio. No pueden cambiar la fisica del jugador ni
    // la geometria con la que colisiona, ni siquiera a traves de cadenas de spawn: un color
    // trigger solo puede propagar colores (copiar canales), un pulse solo colores temporales,
    // un shake solo mueve la camara, un animate solo reproduce animaciones de sprites.
    // IDs verificados contra el analisis de objetos de GDBrowser (GDColon) y documentacion
    // de la comunidad. Cualquier ID fuera de esta lista sigue vetando (fail closed).
    const std::unordered_set<int> kCosmeticTriggerIDs = {
        29, 30,          // color (pre-2.0 / 2.0)
        31,              // start pos (nunca se activa jugando)
        899, 900, 915,   // color (2.1+)
        1006,            // pulse (color temporal)
        1007,            // alpha (opacidad)
        1520,            // shake (pantalla)
        1585,            // animate (monstruos / animaciones visuales)
        1612, 1613       // hide player / show player (visual)
    };

    bool isAudioOnlyTrigger(GameObject* o) {
        return typeinfo_cast<SFXTriggerGameObject*>(o) != nullptr ||
               typeinfo_cast<SongTriggerGameObject*>(o) != nullptr;
    }

    // Un EffectGameObject de la whitelist de portales o cosmético nunca invalida la simulacion.
    bool isExemptEffectObject(GameObject* o) {
        if (!o) return true;
        if (kModeledPortals.contains(o->m_objectID)) return true;      // forma/velocidad/tamano modelados
        if (kCosmeticTriggerIDs.contains(o->m_objectID)) return true;   // solo visuales
        if (isAudioOnlyTrigger(o)) return true;                         // solo audio
        return false;
    }

    struct EffectVetoInfo {
        int objectID = -1;
        int objectType = -1;
        float x = 0.0f;
        float y = 0.0f;
    };

    // Los triggers solo se activan cuando el hitbox del jugador REAL los cruza. El clon no
    // avanza el timeline de triggers, asi que cualquier trigger no exento que el corredor
    // pueda cruzar invalida la simulacion estatica. Filtros v1.5.0:
    //  - X: solo [minX, maxX]. Un trigger completamente detras del jugador nunca vuelve a
    //    dispararse en modo clasico (X siempre crece); el rango viejo [playerX-32, ...] vetaba
    //    trigger lines enteras que el jugador ya habia pasado.
    //  - Y: solo la banda vertical [yMin, yMax] alcanzable por las trayectorias simuladas.
    //    Un trigger flotando fuera de esa banda es inalcanzable para el hitbox del jugador.
    bool hasUnsupportedEffectObjectInBand(PlayLayer* layer, float minX, float maxX,
                                          float yMin, float yMax,
                                          EffectVetoInfo* veto = nullptr) {
        if (!layer || !layer->m_objects || !std::isfinite(minX) || !std::isfinite(maxX) ||
            !std::isfinite(yMin) || !std::isfinite(yMax)) {
            return true; // sin inspeccion fiable no se puede certificar un mundo estatico
        }
        for (auto* obj : CCArrayExt<GameObject*>(layer->m_objects)) {
            if (!obj || obj->m_isDisabled || obj->m_isDisabled2) continue;
            if (!typeinfo_cast<EffectGameObject*>(obj)) continue;
            if (isExemptEffectObject(obj)) continue;
            const auto rect = obj->getObjectRect();
            if (rect.getMaxX() < minX || rect.getMinX() > maxX) continue; // fuera del corredor X
            if (rect.getMaxY() < yMin || rect.getMinY() > yMax) continue; // fuera de la banda Y
            if (veto) {
                veto->objectID = obj->m_objectID;
                veto->objectType = static_cast<int>(obj->m_objectType);
                veto->x = rect.getMidX();
                veto->y = rect.getMidY();
            }
            return true;
        }
        return false;
    }

    // Igual que ShowTrajectory::handlePortal (gravedad omitida igual que en xdBot).
    // v1.6.0: portales de FORMA. Solo se cambian los flags de modo del clon; la fisica de
    // cada modo la ejecuta PlayerObject::update nativo. El seedGhost de cada prediccion
    // restaura los flags desde el jugador real, asi que un portal aplicado en una
    // prediccion no contamina la siguiente.
    void setFormFlags(PlayerObject* player,
                      bool ship, bool bird, bool ball, bool dart,
                      bool robot, bool spider, bool swing) {
        player->m_isShip = ship;
        player->m_isBird = bird;
        player->m_isBall = ball;
        player->m_isDart = dart;
        player->m_isRobot = robot;
        player->m_isSpider = spider;
        player->m_isSwing = swing;
    }

    void handlePortal(PlayerObject* player, int id) {
        switch (id) {
            case 12:  setFormFlags(player, false, false, false, false, false, false, false); break; // cube
            case 13:  setFormFlags(player, true,  false, false, false, false, false, false); break; // ship
            case 47:  setFormFlags(player, false, false, true,  false, false, false, false); break; // ball
            case 111: setFormFlags(player, false, true,  false, false, false, false, false); break; // ufo
            case 660: setFormFlags(player, false, false, false, true,  false, false, false); break; // wave
            case 745: setFormFlags(player, false, false, false, false, true,  false, false); break; // robot
            case 1331: setFormFlags(player, false, false, false, false, false, true,  false); break; // spider
            case 1933: setFormFlags(player, false, false, false, false, false, false, true);  break; // swing
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
    struct SimEvent {
        int step;
        bool down;
        int button;
        bool candidate;
        bool beforeCollision;
        bool syntheticOpposite = false; // prediction-only stress edge, never forwarded to the game
    };

    static SimOut unknown(const char* why) {
        g_unknownReason = why;
        return {SimResult::Unknown, 0};
    }

    template <class P>
    void setCloneY(P* player, float y) {
        player->setPositionY(y);
        if constexpr (requires { player->m_position.y = y; }) {
            player->m_position.y = y;
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
            a.endIsSwing == b.endIsSwing && a.endIsUpsideDown == b.endIsUpsideDown &&
            a.endIsOnGround == b.endIsOnGround && a.endJumpBuffered == b.endJumpBuffered &&
            a.endInputsLocked == b.endInputsLocked && a.endControlsDisabled == b.endControlsDisabled &&
            a.endStateFlipGravity == b.endStateFlipGravity;
    }

    SimOut simulate(
        PlayLayer* layer,
        PlayerObject* real,
        bool down,
        int button,
        int candidateDue,
        std::optional<float> clearTargetX = std::nullopt,
        int stepBudget = kMaxSimSteps,
        bool candidateImmediate = false,
        SimPerturbation perturbation = {},
        int oppositeEdgeAfterCandidateTicks = 0,
        SteadyClock::time_point deadline = SteadyClock::time_point::max()
    ) {
        if (!layer || !real || !g_ghost) return unknown("no-ghost");
        if (g_dtStable < static_cast<int>(kDtCalibrationSamples)) return unknown("dt-window-not-calibrated");
        if (!std::isfinite(g_stepDt) || g_stepDt <= 0.0f) return unknown("invalid-dt");
        if (stepBudget <= 0) return unknown("per-click-budget-exhausted");
        if (!g_ghost->m_collisionLogTop || !g_ghost->m_collisionLogBottom ||
            !g_ghost->m_collisionLogLeft || !g_ghost->m_collisionLogRight) {
            return unknown("ghost-collision-log-null");
        }

        const int base = g_tick + 1;
        if (oppositeEdgeAfterCandidateTicks < 0) return unknown("invalid-opposite-edge-offset");
        std::vector<SimEvent> events;
        events.reserve(g_queue.size() + (oppositeEdgeAfterCandidateTicks > 0 ? 2 : 1));
        for (auto const& q : g_queue) {
            events.push_back({std::max(0, q.tick - base), q.down, q.button, false, false});
        }
        // The actual immediate path calls handleButton synchronously, before the next
        // collision pass. Delayed/queued inputs are dispatched from PlayerObject::update,
        // after that tick's collision pass. Treating both paths identically creates a
        // one-collision-phase error precisely at tight frame-perfect gaps.
        const bool applyCandidateBeforeCollision = candidateImmediate && candidateDue == base;
        const int candidateStep = std::max(0, candidateDue - base);
        events.push_back({candidateStep, down, button, true, applyCandidateBeforeCollision});
        if (oppositeEdgeAfterCandidateTicks > 0) {
            // The actual future raw edge is not known when the current click is predicted.
            // Stress each possible opposite edge within the stage's timing envelope. This
            // synthetic event affects only the ghost, never PlayLayer/the real player.
            events.push_back({candidateStep + oppositeEdgeAfterCandidateTicks, !down, button,
                false, false, true});
        }
        std::stable_sort(events.begin(), events.end(), [](SimEvent const& a, SimEvent const& b) {
            if (a.step != b.step) return a.step < b.step;
            return a.beforeCollision && !b.beforeCollision;
        });

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
        const float dt = g_stepDt * perturbation.dtScale;
        if (!std::isfinite(dt) || dt <= 0.0f) return unknown("perturbed-dt-invalid");
        const float rx = real->getPositionX();
        const float ry = real->getPositionY();
        const double rv = real->m_yVelocity;

        SimOut out{SimResult::Safe, totalSteps};
        float trajMinY = ry;
        float trajMaxY = ry;
        bool candidateApplied = false;
        bool oppositeEdgeApplied = false;
        int oppositeEdgeStep = -1;
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
                g_ghost->m_isPlatformer == real->m_isPlatformer &&
                // Match state that changes the next frame's jump/gravity response, not just pose.
                g_ghost->m_isOnGround == real->m_isOnGround &&
                g_ghost->m_isOnGround2 == real->m_isOnGround2 &&
                g_ghost->m_isOnGround3 == real->m_isOnGround3 &&
                g_ghost->m_isOnGround4 == real->m_isOnGround4 &&
                g_ghost->m_jumpBuffered == real->m_jumpBuffered &&
                g_ghost->m_wasJumpBuffered == real->m_wasJumpBuffered &&
                g_ghost->m_stateJumpBuffered == real->m_stateJumpBuffered &&
                g_ghost->m_stateFlipGravity == real->m_stateFlipGravity &&
                g_ghost->m_controlsDisabled == real->m_controlsDisabled &&
                g_ghost->m_inputsLocked == real->m_inputsLocked &&
                g_ghost->m_isOnSlope == real->m_isOnSlope &&
                g_ghost->m_wasOnSlope == real->m_wasOnSlope &&
                g_ghost->m_isCollidingWithSlope == real->m_isCollidingWithSlope &&
                nearFloat(g_ghost->m_gravity, real->m_gravity, 0.0001f) &&
                nearFloat(g_ghost->m_fallSpeed, real->m_fallSpeed, 0.001f) &&
                nearFloat(g_ghost->m_yStart, real->m_yStart, 0.001f) &&
                nearFloat(g_ghost->m_slopeVelocity, real->m_slopeVelocity, 0.001f) &&
                nearFloat(g_ghost->m_currentSlopeYVelocity, real->m_currentSlopeYVelocity, 0.001f);

            if (!g_warnedSeed) {
                g_warnedSeed = true;
                log::info("[IB] first seed check: {}", seedOK ? "OK" : "MISMATCH");
            }

            if (!seedOK) {
                out = unknown("ghost-seed-mismatch");
            } else {
                // Candidate stress tests deliberately perturb the copy by tiny amounts. A
                // candidate that only survives one exact floating-point trajectory is too
                // brittle to authorize an automated delay.
                if (perturbation.startYOffset != 0.0f) {
                    setCloneY(g_ghost, g_ghost->getPositionY() + perturbation.startYOffset);
                }
                if (perturbation.yVelocityOffset != 0.0f) {
                    g_ghost->m_yVelocity += perturbation.yVelocityOffset;
                }
                size_t next = 0;
                auto applySimEvent = [&](SimEvent const& event) {
                    auto pb = static_cast<PlayerButton>(event.button);
                    if (event.down) g_ghost->pushButton(pb);
                    else g_ghost->releaseButton(pb);
                    if (event.candidate) candidateApplied = true;
                    if (event.syntheticOpposite) {
                        oppositeEdgeApplied = true;
                        oppositeEdgeStep = event.step;
                    }
                };

                for (int step = 0; step < totalSteps; ++step) {
                    // v1.6.3: posicion del clon en este paso (antes de la pasada de
                    // colisiones). traj[step] = posicion tras `step` updates.
                    if (out.traj.size() < 256) {
                        out.traj.emplace_back(g_ghost->getPositionX(), g_ghost->getPositionY());
                    }
                    if (SteadyClock::now() >= deadline) {
                        out = unknown("prediction-wall-budget-exhausted");
                        out.steps = step;
                        break;
                    }
                    // Direct inputs are already active before the next collision pass. Inputs
                    // held in the queue are applied after the due tick's collision pass, matching
                    // dispatchPending() from the PlayerObject::update hook.
                    while (next < events.size() && events[next].step <= step &&
                           events[next].beforeCollision) {
                        applySimEvent(events[next++]);
                    }

                    g_ghost->m_collisionLogTop->removeAllObjects();
                    g_ghost->m_collisionLogBottom->removeAllObjects();
                    g_ghost->m_collisionLogLeft->removeAllObjects();
                    g_ghost->m_collisionLogRight->removeAllObjects();
                    layer->checkCollisions(g_ghost, dt, false);

                    if (g_unmodeled) {
                        out = unknown("unmodeled-object-or-trigger");
                        out.steps = step + 1;
                        log::debug("[IB] sim step {} hit unmodeled object: id={} type={}",
                            step, g_unmodeledObjectID, g_unmodeledObjectType);
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
                        const bool oppositeEdgeSettled = oppositeEdgeAfterCandidateTicks == 0 ||
                            (oppositeEdgeApplied && step - oppositeEdgeStep >= kPostClearanceSteps);
                        if (step - targetCrossStep >= kPostClearanceSteps && oppositeEdgeSettled) {
                            // v1.6.4: "out = {SimResult::Safe, step+1}" reasignaba el SimOut
                            // entero y BORRABA out.traj: chosenTraj llegaba vacio y la
                            // validacion por edge de los SAVED se descartaba sin evidencia.
                            out.result = SimResult::Safe;
                            out.steps = step + 1;
                            clearedTarget = true;
                            break;
                        }
                    }

                    while (next < events.size() && events[next].step <= step) {
                        // beforeCollision events were consumed above. Because equal-step events
                        // are sorted with those first, all remaining events belong after collision.
                        applySimEvent(events[next++]);
                    }

                    if (step + 1 >= totalSteps) break;

                    const float beforeX = g_ghost->getPositionX();
                    g_ghost->update(dt);
                    const float movedX = g_ghost->getPositionX() - beforeX;
                    const float afterY = g_ghost->getPositionY();
                    if (afterY < trajMinY) trajMinY = afterY;
                    if (afterY > trajMaxY) trajMaxY = afterY;

                    // Do not synthesize horizontal movement. The clone must advance through
                    // PlayerObject::update like xdBot's native trajectory. If this state does not
                    // move X naturally, fail closed instead of making a path by hand.
                    if (std::abs(movedX) < 0.0001f) {
                        if (!g_warnedX) {
                            g_warnedX = true;
                            log::warn("[IB] native clone X did not advance; refusing prediction");
                        }
                        out = unknown("ghost-x-did-not-advance-natively");
                        out.steps = step + 1;
                        break;
                    }

                    g_ghost->updateRotation(dt);
                    g_ghost->updatePlayerScale();
                }
            }
        }

        if (g_ghost) {
            out.endX = g_ghost->getPositionX();
            out.endY = g_ghost->getPositionY();
            out.minY = trajMinY;
            out.maxY = trajMaxY;
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
            out.endIsOnGround = g_ghost->m_isOnGround;
            out.endJumpBuffered = g_ghost->m_jumpBuffered;
            out.endInputsLocked = g_ghost->m_inputsLocked;
            out.endControlsDisabled = g_ghost->m_controlsDisabled;
            out.endStateFlipGravity = g_ghost->m_stateFlipGravity;
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

    struct PredictionTimer {
        SteadyClock::time_point start = SteadyClock::now();
        ~PredictionTimer() {
            g_lastPredictionMicros = std::chrono::duration_cast<std::chrono::microseconds>(
                SteadyClock::now() - start).count();
        }
    };

    // v1.6.1: el veredicto UNKNOWN por objeto/trigger no modelado pega el ID en la
    // misma linea del veredicto (antes habia que buscar la linea "sim step" anterior).
    // g_unmodeledObjectID se resetea en cada SimulationScope, y cualquier sim que lo
    // marca termina rompiendo la busqueda como Unknown, asi que aqui es del clic actual.
    std::string unmodeledSuffix() {
        return g_unmodeledObjectID >= 0
            ? fmt::format(" [unmodeledID={} type={}]", g_unmodeledObjectID, g_unmodeledObjectType)
            : std::string();
    }

    int predictDelay(PlayLayer* layer, PlayerObject* player, bool down, int button,
        int inheritedPressDelay = 0) {
        const int safeFallbackDelay = std::max(0, inheritedPressDelay);
        const int limit = limitFor(player);
        if (!layer || !player) { ++g_skipOther; return safeFallbackDelay; }
        if (limit <= 0)        { ++g_skipOff; return safeFallbackDelay; }
        // On the first attempt there is no observed clock yet, so fail open and keep the
        // user's immediate input. On retries, a previously stable measured dt window may be
        // reused at tick 0; otherwise every first input after reset would be skipped forever.
        if (g_tick < 2 && g_dtStable < static_cast<int>(kDtCalibrationSamples)) {
            ++g_skipEarly;
            g_unknownReason = "early-input-before-physics-calibration";
            return safeFallbackDelay;
        }
        if (player->m_isDead)  { ++g_skipDead; return safeFallbackDelay; }
        if (player->m_isPlatformer || button != 1 || layer->m_gameState.m_isDualMode) {
            ++g_skipOther;
            return safeFallbackDelay; // platformer, dual, or directional input is not modeled
        }
        if (!ensureGhost(layer)) { ++g_skipNoGhost; return safeFallbackDelay; }

        const int key = buttonKey(button, true);
        auto keyIt = g_keys.find(key);
        if (keyIt != g_keys.end() && keyIt->second.pending > 0) {
            ++g_skipOther;
            return safeFallbackDelay; // never predict a second press/release while the matching key is pending
        }

        const int baselineTick = g_tick + 1;
        const int requestedBaselineDelay = std::max(0, inheritedPressDelay);
        const int immediateDue = effectiveDue(key, requestedBaselineDelay);
        const int baselineDelay = std::max(0, immediateDue - baselineTick);
        if (baselineDelay > limit) {
            ++g_skipOther;
            log::debug("[IB] {} tick {} -> PRESERVE-DELAY +{} (stage cap {})",
                down ? "PRESS" : "RELEASE", g_tick, baselineDelay, limit);
            return baselineDelay; // preserve the paired press/release offset even outside search cap
        }

        PredictionTimer timer;
        ++g_clicksPredicted;
        g_unknownReason = "";
        const auto deadline = SteadyClock::now() + kPredictionWallBudget;

        // A raw input with no queue is applied synchronously. Otherwise even a zero-delay event
        // is queued and lands after the due tick's collision pass. This boolean fixes the phase
        // used by the baseline simulation; candidate delays > baseline are always queued.
        const bool baselineImmediate = g_queue.empty() && baselineDelay == 0;

        ActivationGuard guard(layer);
        int remainingSteps = kMaxStepsPerInput;
        // Banda Y exacta alcanzada por TODAS las simulaciones de este clic (baseline,
        // candidatos, centro, envolventes y ramas de estres). El veto final de triggers
        // usa esta banda: si un trigger no exento es alcanzable por alguna trayectoria
        // simulada, el mundo no es estatico para el clon y no se autoriza el retraso.
        float bandMinY = player->getPositionY();
        float bandMaxY = player->getPositionY();
        auto runSimulation = [&](int due, std::optional<float> clearTarget = std::nullopt,
                                 bool directInput = false,
                                 SimPerturbation perturbation = {},
                                 int oppositeEdgeAfterTicks = 0) -> SimOut {
            if (remainingSteps <= 0) return unknown("per-click-budget-exhausted");
            if (SteadyClock::now() >= deadline) return unknown("prediction-wall-budget-exhausted");
            const SimOut result = simulate(layer, player, down, button, due, clearTarget,
                std::min(kMaxSimSteps, remainingSteps), directInput, perturbation,
                oppositeEdgeAfterTicks, deadline);
            remainingSteps = std::max(0, remainingSteps - std::max(1, result.steps));
            if (result.result != SimResult::Unknown) {
                bandMinY = std::min(bandMinY, result.minY);
                bandMaxY = std::max(bandMaxY, result.maxY);
            }
            if (SteadyClock::now() >= deadline) return unknown("prediction-wall-budget-exhausted");
            return result;
        };

        const SimOut base = runSimulation(immediateDue, std::nullopt, baselineImmediate);
        guard.restore();

        int chosen = baselineDelay;
        int chosenDue = immediateDue;

        if (base.result == SimResult::Safe) {
            // v1.6.2: NO se libera la pausa aqui. "El baseline no murio de inmediato" es
            // evidencia debil (mira solo el horizonte corto); la pausa se libera cuando
            // la TRAYECTORIA del jugador real coincide con la del clon. Se arma el chequeo
            // con la trayectoria por paso: si el mundo real diverge, MODEL-DIVERGENCE.
            // v1.6.3: deadline con off-by-one corregido (el clon hace steps-1 updates
            // reales; el ultimo paso solo chequea colision y sale antes del update).
            g_realityCheck.active = true;
            g_realityCheck.expectDeath = false;
            g_realityCheck.steps = base.steps;
            g_realityCheck.expectX = base.endX;
            g_realityCheck.expectY = base.endY;
            g_realityCheck.expectDeathX = 0.0f;
            g_realityCheck.expectDeathObjectID = -1;
            g_realityCheck.expectDeathObject = nullptr;
            g_realityCheck.armedInSpawnWindow = (g_tick <= 3);
            g_realityCheck.down = down;
            g_realityCheck.button = button;
            g_realityCheck.armSerialTick = g_physicsSerialTick;
            g_realityCheck.deadlineSerialTick = g_physicsSerialTick + std::max(1, base.steps - 1);
            g_realityCheck.traj = base.traj;
            ++g_statKept;
            if constexpr (kLogVerdicts) {
                log::debug("[IB] {} tick {} -> NO-IMMEDIATE-DEATH (lookahead={} steps; {} budget steps left)",
                    down ? "PRESS" : "RELEASE", g_tick, base.steps, remainingSteps);
            }
        }
        else if (base.result == SimResult::Unknown) {
            ++g_statUnknown;
            if constexpr (kLogVerdicts) {
                log::debug("[IB] {} tick {} -> UNKNOWN ({}){}",
                    down ? "PRESS" : "RELEASE", g_tick, g_unknownReason, unmodeledSuffix());
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
                return safeFallbackDelay;
            }

            const SimOut baselineConfirm = runSimulation(immediateDue, std::nullopt, baselineImmediate);
            guard.restore();
            if (!sameBaselineDeath(base, baselineConfirm)) {
                ++g_statUnknown;
                g_unknownReason = "nondeterministic-baseline-death";
                log::warn("[IB] tick {} rejected: baseline death did not reproduce ({} vs {})",
                    g_tick, base.steps, baselineConfirm.steps);
                return safeFallbackDelay;
            }

            // ---- v1.6.2: chequeo de realidad (modo muerte prevista) ----
            // Esta muerte de referencia solo deberia verse en la realidad si el input
            // pasa tal cual (fail open). Programa el chequeo: si el serialTick de la
            // muerte prevista llega con el jugador real vivo y sin edges nuevos, el
            // baseline era una muerte fantasma del modelo, no del mundo.
            // v1.6.3: se guarda el objeto y el X previstos: la muerte real solo cuenta
            // como "baseline acerto" (y como escape de spawn) si coincide con ellos.
            g_realityCheck.active = true;
            g_realityCheck.expectDeath = true;
            g_realityCheck.steps = base.steps;
            g_realityCheck.expectX = 0.0f;
            g_realityCheck.expectY = 0.0f;
            g_realityCheck.expectDeathX = base.deathX;
            g_realityCheck.expectDeathObjectID = base.deathObject ? base.deathObject->m_objectID : -1;
            g_realityCheck.expectDeathObject = base.deathObject;
            g_realityCheck.armedInSpawnWindow = (g_tick <= 3);
            g_realityCheck.down = down;
            g_realityCheck.button = button;
            g_realityCheck.armSerialTick = g_physicsSerialTick;
            g_realityCheck.deadlineSerialTick = g_physicsSerialTick + base.steps + 3;
            g_realityCheck.traj.clear();

            // v1.6.2: muerte del clon en <=2 pasos con el intento recien arrancado
            // (g_tick<=3). El respawn asienta colisiones en el jugador real que el clon
            // recien sembrado no replica (el log 20.22.21 muestra 36 muertes asi contra
            // un objeto con objectRight=3.00 mientras el jugador real vive ~160 ticks).
            // No se busca retraso contra una muerte en la que no se confia. ESCAPE: si
            // el jugador real muere de verdad en <=2 pasos dentro de la ventana prevista
            // (startpos pegada a un pico), g_spawnDeathsReal lo desactiva para siempre
            // en este nivel: la muerte de spawn era real.
            if (base.steps <= 2 && g_tick <= 3 && !g_spawnDeathsReal) {
                g_predictionsPaused = true;
                ++g_statUnknown;
                g_unknownReason = "baseline-death-at-spawn-state";
                log::debug("[IB] {} tick {} -> UNKNOWN ({}; cloneDiedStep={} objectRight={:.2f} deathX={:.2f} dart={} y={:.2f})",
                    down ? "PRESS" : "RELEASE", g_tick, g_unknownReason, base.steps,
                    base.deathObjectRight, base.deathX,
                    player->m_isDart, player->getPositionY());
                return safeFallbackDelay;
            }

            // v1.6.2: el modelo esta en pausa y seguimos en la ventana de spawn
            // (g_tick<=3): el estado de arranque todavia no coincide con la realidad, no
            // gastes la busqueda. A mitad de intento (g_tick>3) se busca NORMAL aunque
            // haya pausa: el estado ya no es el de spawn y bloquear ahi impide saves
            // reales. Si ese clic a mitad de intento encuentra solucion, su chequeo de
            // trayectoria valida el modelo y libera la pausa si coincide.
            if (g_predictionsPaused && g_tick <= 3) {
                ++g_statUnknown;
                g_unknownReason = "phantom-guard-paused";
                log::debug("[IB] {} tick {} -> UNKNOWN ({}; phantomDeaths={})",
                    down ? "PRESS" : "RELEASE", g_tick, g_unknownReason, g_phantomDeaths);
                return safeFallbackDelay;
            }

            // ---- v1.6.3: gate de autorizacion de SAVEDs ----
            // La validacion del modelo debe ser un requisito, no solo un diagnostico:
            // si nunca se ha confirmado la trayectoria (o divergio hace poco), autorizar
            // retrasos seria confiar el input a un modelo sin respaldo. Fail open: el
            // clic pasa crudo, sin retraso. Los KEPTs siguen armando chequeos; cada
            // coincidencia de trayectoria (deadline o edge) acumula evidencia y, con
            // kMinTrajectoryMatchesForSave, desbloquea los SAVEDs.
            if (!saveAuthorizationValidated()) {
                ++g_statUnknown;
                g_unknownReason = "save-authorization-unvalidated-model";
                log::debug("[IB] {} tick {} -> UNKNOWN ({}; matches={} lastDivergenceSerial={} now={})",
                    down ? "PRESS" : "RELEASE", g_tick, g_unknownReason, g_trajectoryMatches,
                    g_lastDivergenceSerial, g_physicsSerialTick);
                return safeFallbackDelay;
            }

            const auto playerRect = player->GameObject::getObjectRect();
            const float halfPlayerWidth = std::max(
                kMinConservativeHalfWidth,
                std::abs(playerRect.size.width) * 0.5f
            );
            const float hazardClearTargetX = base.deathObjectRight + halfPlayerWidth + kHazardClearancePadding;
            // Do not call one cleared obstacle "safe" immediately. Continue through a 24-unit
            // corridor so nearby chained hazards are checked by native collision simulation.
            const float validationTargetX = hazardClearTargetX + kPostClearanceDistance;
            if (!std::isfinite(validationTargetX) ||
                hazardClearTargetX <= player->getPositionX() + 0.1f ||
                hazardClearTargetX <= base.deathX + 0.1f || validationTargetX <= hazardClearTargetX) {
                ++g_statUnknown;
                g_unknownReason = "invalid-hazard-validation-target";
                log::debug("[IB] tick {} -> UNKNOWN ({}) hazardX={:.2f} validationX={:.2f} deathX={:.2f}",
                    g_tick, g_unknownReason, hazardClearTargetX, validationTargetX, base.deathX);
                return safeFallbackDelay;
            }

            // Geometry moved by actions/triggers is not advanced by checkCollisions(clone).
            // If a physical moving/animated object exists in the predicted X corridor, do not
            // use the static-world simulation to authorize a delayed click.
            if (hasDynamicPhysicalObjectInXRange(
                    layer, player->getPositionX() - 32.0f, validationTargetX + 24.0f)) {
                ++g_statUnknown;
                g_unknownReason = "dynamic-physical-object-in-corridor";
                log::debug("[IB] tick {} -> UNKNOWN ({})", g_tick, g_unknownReason);
                return safeFallbackDelay;
            }

            const auto calibratedEdgeOffsets = futureEdgeOffsets(key, down, limit);
            if (calibratedEdgeOffsets.empty()) {
                ++g_statUnknown;
                g_unknownReason = "future-edge-history-not-calibrated";
                auto profileIt = g_edgeProfiles.find(key);
                const size_t available = profileIt == g_edgeProfiles.end() ? 0 :
                    (down ? profileIt->second.holdTicks.size() : profileIt->second.releaseGapTicks.size());
                log::debug("[IB] {} tick {} -> UNKNOWN (edge history {}/{}; serialTick={}, profile={} samples)",
                    down ? "PRESS" : "RELEASE", g_tick, available, kMinimumEdgeSamples,
                    g_physicsSerialTick, down ? "hold" : "gap");
                return safeFallbackDelay;
            }

            // ---- Veto temprano de triggers (v1.5.0) ----
            // Un trigger no exento (mover/toggle/spawn/count/portal de gravedad...) solo
            // invalida la prediccion si el jugador puede cruzarlo dentro del corredor. La
            // version vieja rechazaba CUALQUIER EffectGameObject en [playerX-32, valX+24],
            // sin importar Y ni tipo: con una trigger line normal al inicio del nivel (colores,
            // bg, shake...) el 100% de las predicciones acababa en UNKNOWN y el input buffer
            // nunca salvaba nada. Ahora:
            //  - X: desde el borde del hitbox del jugador (lo que queda detras ya no puede
            //    volver a dispararse en modo clasico) hasta el final del corredor de validacion.
            //  - Y: banda conservadora que cubre la baseline + toda excursion posible de los
            //    candidatos y del estres de release aprendido.
            //  - whitelist: triggers puramente visuales/audio nunca vetan.
            int maxEdgeOffset = 0;
            for (int n : calibratedEdgeOffsets) maxEdgeOffset = std::max(maxEdgeOffset, n);
            // Cota de |dy| por subpaso: la onda es diagonal (|dy| = |dx| ~ 1.3 en 1x);
            // usar el suelo 3.5 en wave infla la banda y veta triggers inalcanzables.
            // El resto de modos puede caer mas rapido que su dx: suelo conservador.
            // v1.6.3: justo despues de un reset g_lastDx es 0 y el floor 0.5 subestimaba
            // la excursion. Fallback con la velocidad nativa medida del jugador real.
            float dxPerTickGuess = std::abs(g_lastDx);
            if (!(dxPerTickGuess > 0.0f) || !std::isfinite(dxPerTickGuess)) {
                dxPerTickGuess = std::abs(static_cast<float>(player->getCurrentXVelocity()) * g_stepDt);
            }
            const float dyPerTickGuess = (player->m_isDart)
                ? std::max(dxPerTickGuess, 0.5f)
                : std::max(dxPerTickGuess, 3.5f);
            // v1.6.2: floor 96 -> 48: la banda era mas ancha que cualquier salto de
            // corredor; el veto exacto post-candidato sigue de respaldo fail-closed.
            const float coarseExcursion = std::max(48.0f,
                (static_cast<float>(maxEdgeOffset) + static_cast<float>(kHorizon) +
                 static_cast<float>(limit) + 8.0f) * dyPerTickGuess);
            const float coarseMinY = std::min(base.minY, player->getPositionY()) - coarseExcursion;
            const float coarseMaxY = std::max(base.maxY, player->getPositionY()) + coarseExcursion;
            EffectVetoInfo corridorVeto;
            if (hasUnsupportedEffectObjectInBand(
                    layer, playerRect.getMinX(), validationTargetX + 24.0f,
                    coarseMinY, coarseMaxY, &corridorVeto)) {
                ++g_statUnknown;
                g_unknownReason = "unsupported-effect-object-in-corridor";
                log::debug("[IB] {} tick {} -> UNKNOWN ({}) vetoedObjectID={} type={} x={:.2f} y={:.2f}",
                    down ? "PRESS" : "RELEASE", g_tick, g_unknownReason,
                    corridorVeto.objectID, corridorVeto.objectType,
                    corridorVeto.x, corridorVeto.y);
                return safeFallbackDelay;
            }

            bool found = false;
            bool unstable = false;
            int unknownCands = 0;
            int fragileCandidates = 0;
            int bestSurvive = 0;
            int bestDelay = 0;
            int chosenClearSteps = 0;
            int chosenEdgeBranches = 0;
            float chosenEndX = 0.0f;
            float chosenEndY = 0.0f;
            std::vector<std::pair<float, float>> chosenTraj;

            int lastDueChecked = immediateDue;
            for (int totalDelay = baselineDelay + 1; totalDelay <= limit; ++totalDelay) {
                if (remainingSteps <= 0) {
                    g_unknownReason = "per-click-budget-exhausted";
                    unstable = true;
                    break;
                }
                const int due = effectiveDue(key, totalDelay);
                const int effectiveDelay = due - baselineTick;
                if (due == lastDueChecked) continue;
                lastDueChecked = due;
                if (effectiveDelay <= baselineDelay || effectiveDelay > limit) continue;

                const SimOut r = runSimulation(due, validationTargetX);
                guard.restore();

                if (r.result == SimResult::Safe) {
                    // A positive verdict is replayed from a freshly seeded clone. The same
                    // clearance step and end-state must be reproduced before any click moves.
                    const SimOut confirm = runSimulation(due, validationTargetX);
                    guard.restore();
                    if (!sameSafeClearance(r, confirm)) {
                        ++unknownCands;
                        g_unknownReason = "nondeterministic-safe-candidate";
                        log::warn("[IB] tick {} delay {} rejected: clearance replay differs ({} vs {})",
                            g_tick, effectiveDelay, r.steps, confirm.steps);
                        unstable = true;
                        break;
                    }

                    // Robustness envelope: validate the chosen timing under small, symmetric
                    // perturbations of timestep, initial Y, and Y velocity. These are not claimed
                    // to be alternate GD physics; they are a conservative sensitivity check for
                    // a candidate that depends on a razor-thin numerical boundary.
                    const SimPerturbation lowerEnvelope{0.999f, -0.05f, -0.01f};
                    const SimOut lower = runSimulation(due, validationTargetX, false, lowerEnvelope);
                    guard.restore();
                    if (lower.result == SimResult::Unknown) {
                        ++unknownCands;
                        g_unknownReason = "lower-sensitivity-envelope-unknown";
                        unstable = true;
                        break;
                    }
                    if (lower.result != SimResult::Safe) {
                        ++fragileCandidates;
                        continue;
                    }

                    const SimPerturbation upperEnvelope{1.001f, 0.05f, 0.01f};
                    const SimOut upper = runSimulation(due, validationTargetX, false, upperEnvelope);
                    guard.restore();
                    if (upper.result == SimResult::Unknown) {
                        ++unknownCands;
                        g_unknownReason = "upper-sensitivity-envelope-unknown";
                        unstable = true;
                        break;
                    }
                    if (upper.result != SimResult::Safe) {
                        ++fragileCandidates;
                        continue;
                    }

                    // The original 3-tick margin made every SAVED impossible when the wave cap
                    // was 2: a fatal baseline plus three safe delays cannot fit in that range.
                    // Use a 3-tick contiguous window where available, otherwise a 2-tick window
                    // and choose the later tick. Sensitivity and replay checks still apply.
                    const bool twoTickWindow = (limit - baselineDelay) < 3;
                    const int centerRequested = totalDelay + 1;
                    const int tailRequested = totalDelay + 2;
                    if (centerRequested > limit || (!twoTickWindow && tailRequested > limit)) {
                        ++fragileCandidates;
                        continue;
                    }
                    const int centerDue = effectiveDue(key, centerRequested);
                    const int tailDue = twoTickWindow ? centerDue : effectiveDue(key, tailRequested);
                    const int centerDelay = centerDue - baselineTick;
                    const int tailDelay = tailDue - baselineTick;
                    if (centerDelay != effectiveDelay + 1 ||
                        (!twoTickWindow && (tailDelay != centerDelay + 1 || tailDelay > limit))) {
                        ++fragileCandidates;
                        continue;
                    }

                    const SimOut centerProbe = runSimulation(centerDue, validationTargetX);
                    guard.restore();
                    if (centerProbe.result == SimResult::Unknown) {
                        ++unknownCands;
                        g_unknownReason = "timing-window-center-unknown";
                        unstable = true;
                        break;
                    }
                    if (centerProbe.result != SimResult::Safe) {
                        ++fragileCandidates;
                        continue;
                    }
                    if (!twoTickWindow) {
                        const SimOut tailProbe = runSimulation(tailDue, validationTargetX);
                        guard.restore();
                        if (tailProbe.result == SimResult::Unknown) {
                            ++unknownCands;
                            g_unknownReason = "timing-window-tail-unknown";
                            unstable = true;
                            break;
                        }
                        if (tailProbe.result != SimResult::Safe) {
                            ++fragileCandidates;
                            continue;
                        }
                    }

                    // Replay the actual selected center timing; the first robust tick is not
                    // the one sent to the game. The center itself must pass replay + envelope.
                    const SimOut centerConfirm = runSimulation(centerDue, validationTargetX);
                    guard.restore();
                    if (!sameSafeClearance(centerProbe, centerConfirm)) {
                        ++unknownCands;
                        g_unknownReason = "timing-window-center-replay-differs";
                        unstable = true;
                        break;
                    }

                    const SimOut centerLower = runSimulation(centerDue, validationTargetX, false, lowerEnvelope);
                    guard.restore();
                    if (centerLower.result == SimResult::Unknown) {
                        ++unknownCands;
                        g_unknownReason = "timing-window-center-lower-unknown";
                        unstable = true;
                        break;
                    }
                    if (centerLower.result != SimResult::Safe) {
                        ++fragileCandidates;
                        continue;
                    }

                    const SimOut centerUpper = runSimulation(centerDue, validationTargetX, false, upperEnvelope);
                    guard.restore();
                    if (centerUpper.result == SimResult::Unknown) {
                        ++unknownCands;
                        g_unknownReason = "timing-window-center-upper-unknown";
                        unstable = true;
                        break;
                    }
                    if (centerUpper.result != SimResult::Safe) {
                        ++fragileCandidates;
                        continue;
                    }

                    // Test only observed recent hold/gap durations, not every arbitrary tick.
                    // The all-offset sweep made wave candidates fail on hypothetical edge times
                    // that the player never uses and routinely exhausted the work budget.
                    bool inputEdgeProfileRobust = true;
                    int inputEdgeBranchesPassed = 0;
                    for (int oppositeOffset : calibratedEdgeOffsets) {
                        const SimOut edgeBranch = runSimulation(
                            centerDue, validationTargetX, false, {}, oppositeOffset);
                        guard.restore();
                        if (edgeBranch.result == SimResult::Unknown) {
                            ++unknownCands;
                            g_unknownReason = "measured-input-edge-branch-unknown";
                            unstable = true;
                            inputEdgeProfileRobust = false;
                            break;
                        }
                        if (edgeBranch.result != SimResult::Safe) {
                            ++fragileCandidates;
                            inputEdgeProfileRobust = false;
                            break;
                        }
                        ++inputEdgeBranchesPassed;
                    }
                    if (unstable) break;
                    if (!inputEdgeProfileRobust) continue;

                    // ---- Veto exacto de triggers (v1.5.0) ----
                    // Banda Y = union de TODAS las trayectorias simuladas para este clic.
                    // Si un trigger no exento puede cruzarse dentro del corredor X, la
                    // simulacion estatica no describe el mundo real: fail closed.
                    EffectVetoInfo exactVeto;
                    if (hasUnsupportedEffectObjectInBand(
                            layer, playerRect.getMinX(), validationTargetX + 24.0f,
                            bandMinY - kTriggerBandYPad, bandMaxY + kTriggerBandYPad,
                            &exactVeto)) {
                        ++unknownCands;
                        g_unknownReason = "unsupported-effect-object-in-candidate-band";
                        log::debug("[IB] {} tick {} delay {} -> UNKNOWN ({}) vetoedObjectID={} type={} x={:.2f} y={:.2f}",
                            down ? "PRESS" : "RELEASE", g_tick, effectiveDelay, g_unknownReason,
                            exactVeto.objectID, exactVeto.objectType, exactVeto.x, exactVeto.y);
                        unstable = true;
                        break;
                    }

                    chosen = centerDelay;
                    chosenDue = centerDue;
                    chosenClearSteps = centerConfirm.steps;
                    chosenEdgeBranches = inputEdgeBranchesPassed;
                    chosenEndX = centerConfirm.endX;
                    chosenEndY = centerConfirm.endY;
                    chosenTraj = centerConfirm.traj;
                    found = true;
                    break; // center of a three-tick clear window, replayed and sensitivity-checked
                }
                if (r.result == SimResult::Died && r.steps > bestSurvive) {
                    bestSurvive = r.steps;
                    bestDelay = effectiveDelay;
                }
                if (r.result == SimResult::Unknown) {
                    ++unknownCands;
                    // Unknown state (unsupported effect, moving geometry, X mismatch or budget
                    // exhaustion) invalidates this whole search; don't cherry-pick later trials.
                    unstable = true;
                    break;
                }
            }

            if (SteadyClock::now() >= deadline) {
                unstable = true;
                found = false;
                g_unknownReason = "prediction-wall-budget-exhausted";
            }
            if (unstable) {
                ++g_statUnknown;
                if (!g_unknownReason || g_unknownReason[0] == '\0') g_unknownReason = "candidate-simulation-unknown";
                log::debug("[IB] {} tick {} -> UNKNOWN ({}, testedStepsLeft={}){}",
                    down ? "PRESS" : "RELEASE", g_tick, g_unknownReason, remainingSteps, unmodeledSuffix());
            }
            else if (found) {
                // v1.6.2: el retraso autorizado cambia el futuro respecto al baseline, asi
                // que se arma un NUEVO chequeo de realidad con la trayectoria que el clon
                // siguio con el input retrasado (centerConfirm). El jugador real recorrera
                // esa misma trayectoria (el input encolado se aplica en el mismo due); si
                // su posicion en el deadline no coincide, el SAVED era falso:
                // MODEL-DIVERGENCE + pausa. Asi se verifican los SAVED a posteriori.
                // v1.6.3: deadline con off-by-one corregido y trayectoria por paso para
                // que un edge temprano tambien valide el SAVED.
                g_realityCheck.active = true;
                g_realityCheck.expectDeath = false;
                g_realityCheck.steps = chosenClearSteps;
                g_realityCheck.expectX = chosenEndX;
                g_realityCheck.expectY = chosenEndY;
                g_realityCheck.expectDeathX = 0.0f;
                g_realityCheck.expectDeathObjectID = -1;
                g_realityCheck.expectDeathObject = nullptr;
                g_realityCheck.armedInSpawnWindow = (g_tick <= 3);
                g_realityCheck.down = down;
                g_realityCheck.button = button;
                g_realityCheck.armSerialTick = g_physicsSerialTick;
                g_realityCheck.deadlineSerialTick = g_physicsSerialTick + std::max(1, chosenClearSteps - 1);
                g_realityCheck.traj = chosenTraj;
                ++g_statSaved;
                const int actualDelay = chosenDue - baselineTick;
                const float nominalFrames = actualDelay * g_stepDt * 60.0f;
                ac_console::line(fmt::format(
                    "[InputBuffer] *** SAVED +{} UPDATE-TICKS ({:.2f} measured frames @ 60fps) {}! "
                    "(base death step={} object-right={:.2f} -> validation-X={:.2f}, step={}) [Limit: {}] ***",
                    actualDelay, nominalFrames, down ? "PRESS" : "RELEASE",
                    base.steps, base.deathObjectRight, validationTargetX, chosenClearSteps, limit
                ));
                log::info(
                    "[IB] SAVED profile-validated event={} tick={} delay={} limit={} validationX={:.2f} "
                    "validatedThroughStep={} oppositeEdgeBranches={} remainingBudget={}",
                    down ? "PRESS" : "RELEASE", g_tick, actualDelay, limit, validationTargetX,
                    chosenClearSteps, chosenEdgeBranches, remainingSteps
                );
            }
            else {
                ++g_statNoSolution;
                if constexpr (kLogVerdicts) {
                    log::debug(
                        "[IB] {} tick {} -> NO SAFE CLEARANCE beyond baseline +{} (limit {}) "
                        "(baseDeathStep={}, objectRight={:.2f}, bestDelay={}, surviveSteps={}, "
                        "unknownCandidates={}, fragileCandidates={}, remainingBudget={})",
                        down ? "PRESS" : "RELEASE", g_tick, baselineDelay, limit, base.steps,
                        base.deathObjectRight, bestDelay, bestSurvive, unknownCands,
                        fragileCandidates, remainingSteps
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
            if (down) g_activePressDelay[key] = 0;
            QueuedInputScope scope;
            layer->handleButton(down, button, isPlayer1);
            if (!down) g_activePressDelay.erase(key);
            return;
        }

        const int baseTick = g_tick + 1;
        const int due = effectiveDue(key, delay);
        const int scheduledShift = std::max(0, due - baseTick);
        if (down) g_activePressDelay[key] = scheduledShift;

        auto pos = std::upper_bound(
            g_queue.begin(), g_queue.end(), due,
            [](int v, PendingInput const& item) { return v < item.tick; });
        g_queue.insert(pos, PendingInput{due, down, button, isPlayer1});

        auto& ks = g_keys[key];
        ks.lastDue = std::max(ks.lastDue, due);
        ++ks.pending;
        // This is the logical state of the latest raw edge, not a report of delivery. Clear it
        // when release is captured so a later queued press can establish a new hold shift.
        if (!down) g_activePressDelay.erase(key);
    }

    void dispatchPending(PlayLayer* layer, int nextTick) {
        if (!layer) return;
        if (!layer->m_player1 || layer->m_player1->m_isDead) {
            if (!g_queue.empty() && !g_warnedDeathQueue) {
                g_warnedDeathQueue = true;
                log::warn("[IB] pending inputs discarded because the real player is dead");
            }
            clearInputs();
            return;
        }

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

    void flushPendingNow(PlayLayer* layer) {
        if (!layer || !layer->m_player1 || layer->m_player1->m_isDead) {
            clearInputs();
            return;
        }
        while (!g_queue.empty()) {
            const auto input = g_queue.front();
            g_queue.pop_front();
            QueuedInputScope scope;
            layer->handleButton(input.down, input.button, input.isPlayer1);
        }
        g_keys.clear();
        g_activePressDelay.clear();
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
        ac::clearState(false, false);
        ac::clearStats();

        // ensureGhost cleans up the retained clone from any previous PlayLayer.
        ac::ensureGhost(this);
    }

    void resetLevel() {
        // Resumen automatico en cada reinicio/muerte (no hace falta pulsar F7).
        if (ac::g_clicksSeen > 0 || ac::g_tick > 0) {
            log::info("{}", ac::statusLine("ATTEMPT"));
        }
        ac::clearState(true, true);
        PlayLayer::resetLevel();
        ac::clearState(true, true);
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
        ac::clearState(false, false);
        // v1.6.1: al salir del nivel el estado de spawn cambia por completo; la pausa
        // del guard de muertes fantasma describe la confianza en ESTE nivel.
        ac::g_predictionsPaused = false;
        // v1.6.2: el escape del guard de spawn (muerte real en spawn confirmada) tambien
        // es por nivel: en el siguiente nivel hay que volver a detectarla desde cero.
        ac::g_spawnDeathsReal = false;
        // v1.6.3: la validacion del modelo (matches/divergencias) es por nivel: el estado
        // de spawn y la fisica del siguiente nivel no heredan evidencia.
        ac::g_trajectoryMatches = 0;
        ac::g_lastDivergenceSerial = -1000000;
        ac::destroyGhost();
        ac::g_layer = nullptr;
        PlayLayer::onQuit();
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (ac::g_simulating && player == ac::g_ghost) {
            // v1.6.1: diagnostico de la muerte fantasma en estado de spawn. El log
            // 20.22.21 registra 36 muertes del clon en el paso 1 contra un objeto con
            // objectRight=3.00 siempre en tick 0, mientras el jugador real vive ~160
            // ticks. Rects y posiciones del clon vs jugador real + rect/id/tipo del
            // objeto revelan la causa exacta: (a) rect del clon != real -> hitbox del
            // clon distinta; (b) rects iguales y el objeto solapa a ambos -> objeto
            // que el juego real no cuenta como muerte; (c) posiciones distintas ->
            // estado de spawn sin asentar en el clon.
            // v1.6.2: gate ampliado a g_tick<=3 (v1.6.1 solo cubria tick 0/1).
            if (ac::g_tick <= 3) {
                const auto gr = player->GameObject::getObjectRect();
                auto* realPlayer = this->m_player1;
                if (realPlayer) {
                    const auto rr = realPlayer->GameObject::getObjectRect();
                    log::debug("[IB] CLONE-DEATH ghost=({:.2f},{:.2f}) rect=({:.2f},{:.2f},{:.2f},{:.2f}) | "
                               "real=({:.2f},{:.2f}) rect=({:.2f},{:.2f},{:.2f},{:.2f}) | "
                               "obj id={} type={} rect=({:.2f},{:.2f},{:.2f},{:.2f})",
                        player->getPositionX(), player->getPositionY(),
                        gr.getMinX(), gr.getMinY(), gr.getMaxX(), gr.getMaxY(),
                        realPlayer->getPositionX(), realPlayer->getPositionY(),
                        rr.getMinX(), rr.getMinY(), rr.getMaxX(), rr.getMaxY(),
                        object ? object->m_objectID : -1,
                        object ? static_cast<int>(object->m_objectType) : -1,
                        object ? object->getObjectRect().getMinX() : 0.0f,
                        object ? object->getObjectRect().getMinY() : 0.0f,
                        object ? object->getObjectRect().getMaxX() : 0.0f,
                        object ? object->getObjectRect().getMaxY() : 0.0f);
                } else {
                    log::debug("[IB] CLONE-DEATH ghost=({:.2f},{:.2f}) rect=({:.2f},{:.2f},{:.2f},{:.2f}) | "
                               "real=NULL | obj id={} type={} rect=({:.2f},{:.2f},{:.2f},{:.2f})",
                        player->getPositionX(), player->getPositionY(),
                        gr.getMinX(), gr.getMinY(), gr.getMaxX(), gr.getMaxY(),
                        object ? object->m_objectID : -1,
                        object ? static_cast<int>(object->m_objectType) : -1,
                        object ? object->getObjectRect().getMinX() : 0.0f,
                        object ? object->getObjectRect().getMinY() : 0.0f,
                        object ? object->getObjectRect().getMaxX() : 0.0f,
                        object ? object->getObjectRect().getMaxY() : 0.0f);
                }
            }
            ac::g_cloneDied = true;
            ac::g_cloneDeathX = player ? player->getPositionX() : 0.0f;
            ac::g_cloneDeathObjectKnown = false;
            ac::g_cloneDeathObjectRight = 0.0f;
            ac::g_cloneDeathObject = nullptr;

            GameObject* evidence = object;
            const char* evidenceSource = "destroyPlayer-argument";
            const bool argumentIsUsable = evidence && ac::isClearancePhysical(evidence) &&
                !ac::hasDynamicPhysicalMotion(evidence);
            if (!argumentIsUsable) {
                // Use a second evidence source when the engine's argument is absent or not a
                // usable static target. The fallback scans only this level's objects and requires
                // overlap with the ghost's final hitbox; later clearance validation is still mandatory.
                auto* recovered = ac::findStaticDeathColliderFromObjects(this, player);
                if (recovered) {
                    evidence = recovered;
                    evidenceSource = "static-object-overlap-scan";
                }
            }
            if (evidence && ac::isClearancePhysical(evidence) && !ac::hasDynamicPhysicalMotion(evidence)) {
                const auto rect = evidence->getObjectRect();
                const float right = rect.getMaxX();
                if (std::isfinite(right)) {
                    ac::g_cloneDeathObjectRight = right;
                    ac::g_cloneDeathObjectKnown = true;
                    ac::g_cloneDeathObject = evidence;
                    if (evidence != object) {
                        log::debug("[IB] clone death collider recovered from {}: objectID={} type={} right={:.2f}",
                            evidenceSource, evidence->m_objectID, static_cast<int>(evidence->m_objectType), right);
                    }
                }
            }
            if (!ac::g_cloneDeathObjectKnown) {
                log::debug("[IB] clone death target unresolved: arg={} id={} type={} dynamic={} x={:.2f} y={:.2f}; fail closed",
                    static_cast<void*>(object), object ? object->m_objectID : -1,
                    object ? static_cast<int>(object->m_objectType) : -1,
                    object ? ac::hasDynamicPhysicalMotion(object) : false,
                    ac::g_cloneDeathX, player ? player->getPositionY() : 0.0f);
            }
            return;
        }
        if (!ac::g_simulating) {
            auto* liveLayer = PlayLayer::get();
            if (liveLayer && player == liveLayer->m_player1) {
                // v1.6.3: resolucion del chequeo de realidad cuando el jugador real muere.
                if (ac::g_realityCheck.active) {
                    if (ac::g_realityCheck.expectDeath &&
                        ac::g_physicsSerialTick <= ac::g_realityCheck.deadlineSerialTick) {
                        // v1.6.3: la muerte real solo confirma el baseline si COINCIDE con
                        // la muerte prevista. v1.6.4: la identidad es la INSTANCIA
                        // (puntero), no m_objectID (el tipo: dos picos cualquiera
                        // "coincidian"). v1.6.4: una muerte confirmada tambien valida el
                        // modelo: sin esto, una startpos pegada a un pico jamas abria el
                        // gate (el primer clic ya es el mortal, nunca hay KEPT previo).
                        const bool sameObject = object &&
                            object == ac::g_realityCheck.expectDeathObject;
                        const bool sameX = object && std::abs(
                            player->getPositionX() - ac::g_realityCheck.expectDeathX) <=
                            ac::kRealityDeathXTolerance;
                        if (sameObject && sameX) {
                            ac::g_predictionsPaused = false;
                            ++ac::g_trajectoryMatches;
                            if (ac::g_realityCheck.steps <= 2 && !ac::g_spawnDeathsReal) {
                                ac::g_spawnDeathsReal = true;
                                log::debug("[IB] real spawn death confirmed (steps={} <= 2, objectID={}): "
                                           "spawn guard disabled for this level",
                                    ac::g_realityCheck.steps, ac::g_realityCheck.expectDeathObjectID);
                            }
                            log::debug("[IB] real death matches the predicted one (serialTick={} <= {}, "
                                       "objectID={}, deathX={:.2f} vs {:.2f}): baseline matched reality",
                                ac::g_physicsSerialTick, ac::g_realityCheck.deadlineSerialTick,
                                ac::g_realityCheck.expectDeathObjectID,
                                player->getPositionX(), ac::g_realityCheck.expectDeathX);
                        }
                        else {
                            log::debug("[IB] real death within the predicted window but NOT the predicted "
                                       "death (objectID={} vs {}, deathX={:.2f} vs {:.2f}): no validation, "
                                       "no spawn escape",
                                object ? object->m_objectID : -1, ac::g_realityCheck.expectDeathObjectID,
                                player ? player->getPositionX() : 0.0f, ac::g_realityCheck.expectDeathX);
                        }
                    }
                    else if (!ac::g_realityCheck.expectDeath) {
                        // El modelo decia que la trayectoria era segura (KEPT) o que el
                        // retraso la evitaba (SAVED) y el jugador real murio dentro del
                        // horizonte del chequeo: divergencia del modelo, pausa.
                        ++ac::g_modelDivergences;
                        ac::g_predictionsPaused = true;
                        ac::g_trajectoryMatches = 0;
                        ac::g_lastDivergenceSerial = ac::g_physicsSerialTick;
                        log::warn("[IB] MODEL-DIVERGENCE: el modelo preveia trayectoria segura "
                                  "(expect=({:.2f},{:.2f}) steps={} button={} down={}) pero el jugador real "
                                  "murio en serialTick {}; predicciones en pausa",
                            ac::g_realityCheck.expectX, ac::g_realityCheck.expectY,
                            ac::g_realityCheck.steps, ac::g_realityCheck.button,
                            ac::g_realityCheck.down, ac::g_physicsSerialTick);
                    }
                    // Si expectDeath pero serialTick > deadline: la muerte llego tarde;
                    // el chequeo de realidad ya expiro (la posicion se valida por
                    // trayectoria en PlayerObject::update o el intento termino).
                }
                ac::g_realityCheck.active = false;
                const auto discarded = ac::g_queue.size();
                ac::clearInputs();
                if (discarded > 0) log::warn("[IB] cancelled {} pending edge(s) on player death", discarded);
            }
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

        if (layer->m_gameState.m_isDualMode) {
            if (!ac::g_warnedDualMode) {
                ac::g_warnedDualMode = true;
                log::warn("[IB] dual mode detected: prediction disabled; passing raw input");
            }
            ac::flushPendingNow(layer);
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
        const int key = ac::buttonKey(button, isPlayer1);
        const bool rawEdgeChanged = ac::observeRawInputEdge(key, down);
        auto keyIt = ac::g_keys.find(key);
        const bool keyPending = keyIt != ac::g_keys.end() && keyIt->second.pending > 0;
        auto activeIt = ac::g_activePressDelay.find(key);
        const bool hasActivePress = activeIt != ac::g_activePressDelay.end();
        const int inheritedDelay = hasActivePress ? activeIt->second : 0;

        if (player && ac::kStages[ac::g_stage].all > 0) {
            if (keyPending) {
                // An edge that arrives while a same-button edge is pending cannot be simulated
                // independently. Preserve order and inherit the current press shift.
                delay = inheritedDelay;
                ++ac::g_skipOther;
            } else if (down && hasActivePress) {
                // Duplicate down edge while this hold is active: do not independently retime it.
                delay = inheritedDelay;
                ++ac::g_skipOther;
            } else if (!rawEdgeChanged) {
                // v1.6.0: GD re-delivers the button state at the start of every attempt
                // (re-sync after death/retry). The raw button state did not change, so this is
                // not a real input edge: it carries no new timing evidence and must not spend
                // the prediction budget. Pass it through with the inherited shift, like any
                // other duplicate.
                delay = inheritedDelay;
                ++ac::g_skipOther;
            } else {
                delay = ac::predictDelay(layer, player, down, button,
                    (!down && hasActivePress) ? inheritedDelay : 0);
            }
        } else {
            ++ac::g_skipOff;
            // Turning assistance OFF during a delayed hold must not shift only one edge.
            if (hasActivePress) delay = inheritedDelay;
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
            // v1.5.0: un trigger puramente visual/audio que el clon cruza no puede cambiar la
            // fisica, asi que ya no marca la simulacion como Unknown. Sigue desactivandose
            // durante la pasada de colision para impedir cualquier activacion real.
            const bool exemptEffect =
                typeinfo_cast<EffectGameObject*>(obj) != nullptr && ac::isExemptEffectObject(obj);

            if (nearby && !harmless && !exemptEffect &&
                (!modeled || ac::hasDynamicPhysicalMotion(obj))) {
                if (ac::g_unmodeledObjectID < 0) {
                    ac::g_unmodeledObjectID = obj->m_objectID;
                    ac::g_unmodeledObjectType = static_cast<int>(obj->m_objectType);
                }
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
                if (ac::g_unmodeledObjectID < 0 && p1) {
                    ac::g_unmodeledObjectID = p1->m_objectID;
                    ac::g_unmodeledObjectType = static_cast<int>(p1->m_objectType);
                }
                ac::g_unmodeled = true;
            }
            return false; // never let the clone activate a real level object
        }
        return GJBaseGameLayer::canBeActivatedByPlayer(p0, p1);
    }

    void playerTouchedRing(PlayerObject* p0, RingObject* p1) {
        if (ac::g_simulating) {
            if (p0 == ac::g_ghost) {
                if (ac::g_unmodeledObjectID < 0 && p1) {
                    ac::g_unmodeledObjectID = p1->m_objectID;
                    ac::g_unmodeledObjectType = static_cast<int>(p1->m_objectType);
                }
                ac::g_unmodeled = true;
            }
            return;
        }
        GJBaseGameLayer::playerTouchedRing(p0, p1);
    }

    void playerTouchedTrigger(PlayerObject* p0, EffectGameObject* p1) {
        if (ac::g_simulating) {
            if (p0 == ac::g_ghost && p1 && ac::kModeledPortals.contains(p1->m_objectID)) {
                ac::handlePortal(p0, p1->m_objectID);
            } else {
                if (ac::g_unmodeledObjectID < 0 && p1) {
                    ac::g_unmodeledObjectID = p1->m_objectID;
                    ac::g_unmodeledObjectType = static_cast<int>(p1->m_objectType);
                }
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
        // Increment the monotonic input-history clock before entering the native update: death
        // and resetLevel can happen synchronously inside PlayerObject::update, and the tick that
        // caused that death must be included in the press/release interval.
        if (advancingTick) ++ac::g_physicsSerialTick;
        PlayerObject::update(dt);

        if (advancingTick) {
            ac::observePhysicsDt(dt);
            ac::g_lastDx = getPositionX() - beforeX;
            ++ac::g_tick;

            if (!ac::g_loggedFirstUpdate) {
                ac::g_loggedFirstUpdate = true;
                log::info("[IB] first real update dt={} dx={}", dt, ac::g_lastDx);
            }

            // ---- v1.6.2: resolucion del chequeo de realidad ----
            // El serialTick del deadline ya paso y no llego ningun edge nuevo (eso
            // cancela el chequeo en observeRawInputEdge). Dos modos:
            //  - expectDeath: el baseline predijo muerte; jugador real vivo ->
            //    PHANTOM-DEATH confirmada, pausa. (Si murio, lo maneja destroyPlayer.)
            //  - !expectDeath: KEPT o SAVED; la posicion real debe coincidir con la del
            //    clon (+-kRealityPositionTolerance). Divergencia (o muerte) ->
            //    MODEL-DIVERGENCE, pausa. Coincidencia -> el modelo describe la
            //    realidad: liberar la pausa si estaba activa.
            if (ac::g_realityCheck.active &&
                ac::g_physicsSerialTick >= ac::g_realityCheck.deadlineSerialTick) {
                const bool posMatches =
                    !this->m_isDead &&
                    std::abs(getPositionX() - ac::g_realityCheck.expectX) <= ac::kRealityPositionTolerance &&
                    std::abs(getPositionY() - ac::g_realityCheck.expectY) <= ac::kRealityPositionTolerance;
                if (ac::g_realityCheck.expectDeath) {
                    if (!this->m_isDead) {
                        ac::g_realityCheck.active = false;
                        ++ac::g_phantomDeaths;
                        ac::g_predictionsPaused = true;
                        // v1.6.4: un PHANTOM-DEATH del estado de spawn (chequeo armado con
                        // g_tick<=3) NO envenena la validacion del nivel: en el escenario
                        // del log hay un fantasma por cada clic al reaparecer y el gate de
                        // SAVED casi nunca llegaba a abrirse. El spawn es un problema ya
                        // aislado (guard propio); el modelo a mitad de nivel se valida aparte.
                        if (!ac::g_realityCheck.armedInSpawnWindow) {
                            ac::g_trajectoryMatches = 0;
                            ac::g_lastDivergenceSerial = ac::g_physicsSerialTick;
                        }
                        log::warn("[IB] PHANTOM-DEATH: el baseline predijo muerte del clon en {} pasos "
                                  "(button={} down={} spawnArmed={}) pero el jugador real sigue vivo en el "
                                  "serialTick {}; predicciones en pausa hasta que el modelo vuelva a coincidir "
                                  "con la realidad",
                            ac::g_realityCheck.steps, ac::g_realityCheck.button, ac::g_realityCheck.down,
                            ac::g_realityCheck.armedInSpawnWindow ? 1 : 0,
                            ac::g_physicsSerialTick);
                    }
                    // Si m_isDead: la muerte la resuelve destroyPlayer (libera pausa /
                    // marca spawnDeathsReal si coincide). El chequeo se desactiva alli.
                }
                else {
                    ac::g_realityCheck.active = false;
                    if (posMatches) {
                        ++ac::g_trajectoryMatches;
                        if (ac::g_predictionsPaused) {
                            ac::g_predictionsPaused = false;
                            log::warn("[IB] PHANTOM-GUARD released at tick {}: trajectory matches reality "
                                      "(real=({:.2f},{:.2f}) expect=({:.2f},{:.2f}); matches={})",
                                ac::g_tick, getPositionX(), getPositionY(),
                                ac::g_realityCheck.expectX, ac::g_realityCheck.expectY, ac::g_trajectoryMatches);
                        }
                    }
                    else {
                        ++ac::g_modelDivergences;
                        ac::g_predictionsPaused = true;
                        ac::g_trajectoryMatches = 0;
                        ac::g_lastDivergenceSerial = ac::g_physicsSerialTick;
                        log::warn("[IB] MODEL-DIVERGENCE: tras {} pasos el jugador real quedo en ({:.2f},{:.2f}) "
                                  "pero el modelo preveia ({:.2f},{:.2f}) (button={} down={} dead={}); "
                                  "predicciones en pausa hasta que el modelo vuelva a coincidir con la realidad",
                            ac::g_realityCheck.steps, getPositionX(), getPositionY(),
                            ac::g_realityCheck.expectX, ac::g_realityCheck.expectY,
                            ac::g_realityCheck.button, ac::g_realityCheck.down,
                            this->m_isDead ? 1 : 0);
                    }
                }
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
            if (ac::g_unmodeledObjectID < 0) {
                ac::g_unmodeledObjectID = this->m_objectID;
                ac::g_unmodeledObjectType = static_cast<int>(this->m_objectType);
            }
            ac::g_unmodeled = true; // un trigger cambiaria el nivel: no se modela
            return;
        }
        EffectGameObject::triggerObject(layer, id, groups);
    }
};
