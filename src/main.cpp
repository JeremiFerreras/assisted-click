// Assisted Click - src/main.cpp -- v2.0.0 (REESCRITURA COMPLETA al sistema de buffer probado:
//                                       buffering de clic ciego, sin clon ni simulacion)
//
// QUE HACE (el sistema nuevo, verificado funcionando):
//  - MODOS DE SUELO (cubo, bola, robot, arana): si pulsas en el aire sin tocar un orbe, el
//    clic queda PENDIENTE hasta que tocas suelo o un orbe; ahi se aplica (y se suelta en
//    el mismo paso). Ese es el "SAVED": el clic que llego antes de tiempo se aplica en el
//    momento correcto. Ventana: `stage` (2/4/8/16 ticks u Off).
//  - MODOS DE VUELO: un release demasiado temprano se retrasa hasta el hold minimo del
//    modo (MIN-HOLD), evitando toques que no mueven nada.
//  - WAVE: tres modos — Grid (cuantiza press/release a una rejilla de N steps: cada
//    celda tiene un unico input), Min Gap (minimo N steps entre release y press) y
//    Min Hold (desactivado). Tras un spawn hay GRACE de 16 ticks sin cuantizar.
//  - DUAL: el clic de P1 se reenvia a P2 solo si P2 esta en modo de suelo (un rescate
//    de cubo no debe hacer tap en un wave).
//  - Todo cuenta en STEPS de fisica (240Hz): la ventana en ticks se escala por TPS
//    (60..1000), para convivir con Click Between Frames.
//  - Estado completo que se limpia en muerte/restart/checkpoint (resetEpoch) y
//    botones atascados liberados al reiniciar.
//
// POR QUE SE REESCRIBIO: las versiones 1.x usaban un clon + simulacion nativa para
// PREDECIR si retrasar un clic salvaba (corredor de validacion, vetos de triggers,
// reality checks). Ese motor nunca produjo un SAVED real y su complejidad no aporta:
// el buffer ciego logra el mismo objetivo (que el clic llegue en el momento util)
// sin predecir nada. Todo el maquinario 1.x de PREDICCION (clon, seedGhost, simulate,
// vetos, reality checks, compuerta de autorizacion) se ELIMINO por diseño. Lo que NO
// era del predictor SE CONSERVA: la consola en vivo de Windows (ac_console, heredada
// del 1.x tal cual: AllocConsole + WriteConsoleA), el banner de carga, los mensajes
// de hotkeys y el STATUS con contador de saves.
//
// REQUISITOS EN mod.json (settings que lee este archivo):
//   stage ("2 ticks"/"4 ticks"/"8 ticks"/"16 ticks"/"Off", default "4 ticks"),
//   tps ("60".."1000", default "240"), wave-mode ("Min Hold"/"Min Gap"/"Grid",
//   default "Grid"), cube/ball/robot/spider/ship/ufo/swing/wave-ticks (int, -1 = como
//   stage), key-stage/key-tps-up/key-tps-down/key-debug/key-wave ("F1".."F12",
//   "Insert", "Delete", "Home", "End", "Page Up", "Page Down", "None"), debug (bool).
//
// CONSOLA/LOG: los SAVED se anuncian en la CONSOLA EN VIVO (ventana "InputBuffer -
// Live Console", misma del 1.x), en el log de Geode (*** SAVED ***) y con una
// notificacion en pantalla. Los hotkeys tambien escriben en la consola, y el hotkey
// de debug muestra el STATUS completo (como el F7 antiguo). El boton "Buffer" en la
// pausa abre los ajustes del mod.

#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/Notification.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#ifdef GEODE_IS_WINDOWS
#include <Windows.h>
#endif

using namespace geode::prelude;

// ============================================================================
// Consola en vivo (heredada del 1.x SIN cambios: nada que ver con el predictor).
// Adjunta una consola de Windows al proceso y escribe lineas con WriteConsoleA.
// Nunca libera/desadjunta una consola del host u otro mod; solo crea una si el
// proceso no tiene consola.
// ============================================================================
namespace ac_console {

#ifdef GEODE_IS_WINDOWS

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

// ---- settings --------------------------------------------------------
static constexpr char const* STAGE_NAMES[] = {"2 ticks", "4 ticks", "8 ticks", "16 ticks", "Off"};
static constexpr int STAGES[] = {2, 4, 8, 16, 0};
static constexpr int STAGE_COUNT = 5;
static int g_stage = 1;

static constexpr int WAVE_MAX_TICKS = 8;

// After a spawn, wave Grid / Min Gap are skipped for this many ticks.
static constexpr int SPAWN_GRACE_TICKS = 16;

// Index: 0 cube, 1 ball, 2 robot, 3 spider, 4 ship, 5 ufo, 6 swing, 7 wave.
// Value: -1 = same as main window, 0 = off, >0 = custom ticks.
static constexpr int MODE_COUNT = 8;
static constexpr int MODE_WAVE = 7;
static constexpr char const* MODE_KEYS[] = {
    "cube-ticks", "ball-ticks", "robot-ticks", "spider-ticks",
    "ship-ticks", "ufo-ticks", "swing-ticks", "wave-ticks"
};
static int g_modeTicks[MODE_COUNT] = {-1, -1, -1, -1, -1, -1, -1, -1};

static constexpr char const* WAVE_NAMES[] = {"Min Hold", "Min Gap", "Grid"};
enum WaveMode { WAVE_HOLD = 0, WAVE_GAP = 1, WAVE_GRID = 2 };
static constexpr int WAVE_MODE_COUNT = 3;
static int g_waveMode = WAVE_GRID;

static constexpr char const* TPS_NAMES[] = {"60", "120", "240", "360", "480", "720", "960", "1000"};
static constexpr int TPS_PRESETS[] = {60, 120, 240, 360, 480, 720, 960, 1000};
static constexpr int TPS_COUNT = 8;
static int g_tpsIndex = 2;

static bool g_debug = false;

// Hotkeys. Order: 0 stage, 1 tps up, 2 tps down, 3 debug, 4 wave mode.
static constexpr int KEY_COUNT = 5;
static constexpr char const* KEY_SETTINGS[] = {"key-stage", "key-tps-up", "key-tps-down", "key-debug", "key-wave"};
static int g_keyVK[KEY_COUNT] = {0, 0, 0, 0, 0}; // 0 = none

// Raised whenever the level resets (death, restart, checkpoint).
static int g_resetEpoch = 0;
// ----------------------------------------------------------------------

#define DBG(...) do { if (g_debug) log::info(__VA_ARGS__); } while (0)

static int indexOf(char const* const* names, int n, std::string const& v, int def) {
    for (int i = 0; i < n; i++) if (v == names[i]) return i;
    return def;
}

static int vkFromName(std::string const& n) {
#ifdef GEODE_IS_WINDOWS
    if (n == "None") return 0;
    if (n.size() >= 2 && n[0] == 'F') {
        int num = std::atoi(n.c_str() + 1);
        if (num >= 1 && num <= 12) return VK_F1 + (num - 1);
    }
    if (n == "Insert") return VK_INSERT;
    if (n == "Delete") return VK_DELETE;
    if (n == "Home") return VK_HOME;
    if (n == "End") return VK_END;
    if (n == "Page Up") return VK_PRIOR;
    if (n == "Page Down") return VK_NEXT;
#endif
    return 0;
}

static void loadSettings() {
    auto m = Mod::get();
    g_stage = indexOf(STAGE_NAMES, STAGE_COUNT, m->getSettingValue<std::string>("stage"), 1);
    g_tpsIndex = indexOf(TPS_NAMES, TPS_COUNT, m->getSettingValue<std::string>("tps"), 2);
    g_waveMode = indexOf(WAVE_NAMES, WAVE_MODE_COUNT, m->getSettingValue<std::string>("wave-mode"), WAVE_GRID);
    for (int i = 0; i < MODE_COUNT; i++) {
        g_modeTicks[i] = static_cast<int>(m->getSettingValue<int64_t>(MODE_KEYS[i]));
    }
    for (int i = 0; i < KEY_COUNT; i++) {
        g_keyVK[i] = vkFromName(m->getSettingValue<std::string>(KEY_SETTINGS[i]));
    }
    g_debug = m->getSettingValue<bool>("debug");
}

static char const* waveModeName() { return WAVE_NAMES[g_waveMode]; }

static float stepsPerTick() {
    return TPS_PRESETS[g_tpsIndex] / 240.0f;
}

static int mainLimitSteps() {
    return static_cast<int>(std::lround(STAGES[g_stage] * stepsPerTick()));
}

static bool isFlyMode(PlayerObject* p) {
    return p->m_isShip || p->m_isBird || p->m_isDart || p->m_isSwing;
}

static bool isGroundMode(PlayerObject* p) {
    return !isFlyMode(p);
}

static int modeIndex(PlayerObject* p) {
    if (p->m_isShip) return 4;
    if (p->m_isBird) return 5;
    if (p->m_isDart) return 7;
    if (p->m_isSwing) return 6;
    if (p->m_isBall) return 1;
    if (p->m_isRobot) return 2;
    if (p->m_isSpider) return 3;
    return 0;
}

static int ticksForMode(int idx) {
    int c = g_modeTicks[idx];
    if (c < 0) {
        int t = STAGES[g_stage];
        if (idx == MODE_WAVE && t > 0) t = std::min(t, WAVE_MAX_TICKS);
        return t;
    }
    return c;
}

static int modeLimitSteps(PlayerObject* p) {
    int ticks = ticksForMode(modeIndex(p));
    if (ticks <= 0) return 0;
    return std::max(1, static_cast<int>(std::lround(ticks * stepsPerTick())));
}

static int waveGridSteps() {
    int ticks = ticksForMode(MODE_WAVE);
    if (ticks <= 0) return 0;
    return std::max(1, static_cast<int>(std::lround(ticks * stepsPerTick())));
}

static int spawnGraceSteps() {
    return std::max(1, static_cast<int>(std::lround(SPAWN_GRACE_TICKS * stepsPerTick())));
}

static int ringsTouching(PlayerObject* p) {
    return p->m_touchingRings ? static_cast<int>(p->m_touchingRings->count()) : 0;
}

static char const* modeName(PlayerObject* p) {
    if (p->m_isShip) return "ship";
    if (p->m_isBird) return "ufo";
    if (p->m_isDart) return "wave";
    if (p->m_isSwing) return "swing";
    if (p->m_isBall) return "ball";
    if (p->m_isRobot) return "robot";
    if (p->m_isSpider) return "spider";
    return "cube";
}

// Todo lo que el usuario debe ver en vivo va a la consola Y al log de Geode.
static void announce(std::string const& msg) {
    ac_console::line(msg);
    log::info("{}", msg);
}

static int g_savedCount = 0;

// STATUS completo (equivalente al F7 del 1.x, ahora en el hotkey key-debug).
static std::string statusLine() {
    return fmt::format(
        "[InputBuffer] STATUS | stage: {} | tps: {} ({:.2f} steps/tick) | wave: {} "
        "(grid {} steps) | grace: {} ticks | debug: {} | saves: {}",
        STAGE_NAMES[g_stage], TPS_PRESETS[g_tpsIndex], stepsPerTick(), waveModeName(),
        waveGridSteps(), SPAWN_GRACE_TICKS, g_debug ? "ON" : "OFF", g_savedCount);
}

static void doHotkey(int action) {
    auto m = Mod::get();
    switch (action) {
        case 0:
            g_stage = (g_stage + 1) % STAGE_COUNT;
            m->setSettingValue<std::string>("stage", STAGE_NAMES[g_stage]);
            announce(fmt::format("[InputBuffer] Main window -> {} ({} steps, wave {} steps @ {} TPS)",
                      STAGE_NAMES[g_stage], mainLimitSteps(), waveGridSteps(), TPS_PRESETS[g_tpsIndex]));
            break;
        case 1:
            g_tpsIndex = std::min(g_tpsIndex + 1, TPS_COUNT - 1);
            m->setSettingValue<std::string>("tps", TPS_NAMES[g_tpsIndex]);
            announce(fmt::format("[InputBuffer] TPS -> {} ({:.2f} steps/tick)",
                      TPS_PRESETS[g_tpsIndex], stepsPerTick()));
            break;
        case 2:
            g_tpsIndex = std::max(g_tpsIndex - 1, 0);
            m->setSettingValue<std::string>("tps", TPS_NAMES[g_tpsIndex]);
            announce(fmt::format("[InputBuffer] TPS -> {} ({:.2f} steps/tick)",
                      TPS_PRESETS[g_tpsIndex], stepsPerTick()));
            break;
        case 3:
            g_debug = !g_debug;
            m->setSettingValue<bool>("debug", g_debug);
            announce(fmt::format("[InputBuffer] Debug logging -> {}", g_debug ? "ON" : "OFF"));
            announce(statusLine());
            break;
        case 4:
            g_waveMode = (g_waveMode + 1) % WAVE_MODE_COUNT;
            m->setSettingValue<std::string>("wave-mode", WAVE_NAMES[g_waveMode]);
            announce(fmt::format("[InputBuffer] Wave mode -> {} ({} steps)",
                      waveModeName(), waveGridSteps()));
            break;
    }
}

static void pollKeys() {
#ifdef GEODE_IS_WINDOWS
    static bool prev[KEY_COUNT] = {false, false, false, false, false};
    bool now[KEY_COUNT];
    for (int i = 0; i < KEY_COUNT; i++) {
        now[i] = g_keyVK[i] != 0 && (GetAsyncKeyState(g_keyVK[i]) & 0x8000) != 0;
    }
    for (int i = 0; i < KEY_COUNT; i++) {
        if (now[i] && !prev[i]) doHotkey(i);
    }
    for (int i = 0; i < KEY_COUNT; i++) prev[i] = now[i];
#endif
}

// Banner de la consola (heredado del 1.x): se muestra una sola vez al cargar.
static void showBannerOnce() {
    static bool shown = false;
    if (shown || !ac_console::init()) return;
    shown = true;

    ac_console::line("===================================================");
    ac_console::line("[InputBuffer] Assisted Click v2.0.0 LOADED (buffer ciego: clic pendiente hasta suelo/orbe; min-hold; wave grid/gap; grace de spawn)");
    ac_console::line(fmt::format(
        "[InputBuffer] Stage: {} | TPS: {} | Wave: {} | Debug: {}",
        STAGE_NAMES[g_stage], TPS_PRESETS[g_tpsIndex], waveModeName(), g_debug ? "ON" : "OFF"));
    ac_console::line("[InputBuffer] Hotkeys (settings): key-stage, key-tps-up, key-tps-down, key-debug (muestra STATUS), key-wave");
    ac_console::line("[InputBuffer] TPS debe coincidir con tu setup (240 = CBF desactivado)");
    ac_console::line("===================================================");
}

$on_mod(Loaded) {
    loadSettings();
    listenForSettingChanges<std::string>("stage", [](std::string) { loadSettings(); });
    listenForSettingChanges<std::string>("tps", [](std::string) { loadSettings(); });
    listenForSettingChanges<std::string>("wave-mode", [](std::string) { loadSettings(); });
    for (auto key : MODE_KEYS) {
        listenForSettingChanges<int64_t>(key, [](int64_t) { loadSettings(); });
    }
    for (auto key : KEY_SETTINGS) {
        listenForSettingChanges<std::string>(key, [](std::string) { loadSettings(); });
    }
    listenForSettingChanges<bool>("debug", [](bool) { loadSettings(); });

    showBannerOnce();

    log::info("[InputBuffer] Assisted Click v2.0.0 loaded (buffer ciego: clic pendiente hasta suelo/orbe; min-hold; wave grid/gap; grace de spawn). Main: {}, TPS: {}, wave: {}",
              STAGE_NAMES[g_stage], TPS_PRESETS[g_tpsIndex], waveModeName());
}

class $modify(BufferPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto menu = CCMenu::create();
        auto spr = ButtonSprite::create("Buffer");
        spr->setScale(0.7f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(BufferPause::onBufferSettings));
        menu->addChild(btn);

        auto win = CCDirector::get()->getWinSize();
        menu->setPosition({win.width - 60.f, 30.f});
        this->addChild(menu);
    }

    void onBufferSettings(CCObject*) {
        openSettingsPopup(Mod::get(), false);
    }
};

// Raises the reset flag on death, restart and checkpoint loads. The buffer
// layer clears its state on the next step.
class $modify(BufferReset, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        g_resetEpoch++;
    }

    void resetLevelFromStart() {
        PlayLayer::resetLevelFromStart();
        g_resetEpoch++;
    }

    void loadFromCheckpoint(CheckpointObject* object) {
        PlayLayer::loadFromCheckpoint(object);
        g_resetEpoch++;
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        PlayLayer::destroyPlayer(player, object);
        g_resetEpoch++;
    }
};

struct PState {
    bool held = false;
    bool pending = false;
    int pendingStart = 0;
    int lastRingCount = 0;
    bool releaseNext = false;

    int pressStep = -1;
    bool delayedRelease = false;
    int releaseAt = 0;

    std::vector<std::pair<bool, int>> waveQueue;
    int lastReleaseStep = -1000000;
};

class $modify(BufferLayer, GJBaseGameLayer) {
    struct Fields {
        int step = 0;
        int epoch = 0;
        int graceUntil = 0;
        PState ps[2];
    };

    // True in a one-player level with a dual portal: a single click is sent
    // as player 1 and the game applies it to both icons.
    bool sharedDualInput() {
        return m_gameState.m_isDualMode && m_levelSettings && !m_levelSettings->m_twoPlayerMode;
    }

    // Press or release for the icon at idx. In shared dual input, player 1's
    // events also go to player 2 (rescued clicks only if player 2 is in a
    // landing mode, so a cube rescue does not tap a wave).
    void sendInput(PlayerObject* p, int idx, bool down, bool groundOnly) {
        auto one = [&](PlayerObject* q) {
            if (down) q->pushButton(PlayerButton::Jump);
            else q->releaseButton(PlayerButton::Jump);
        };
        one(p);
        if (idx == 0 && m_player2 && sharedDualInput()) {
            if (!groundOnly || isGroundMode(m_player2)) one(m_player2);
        }
    }

    // Clears our state after a death / restart / checkpoint load.
    void syncState() {
        auto f = m_fields.operator->();
        if (f->epoch == g_resetEpoch) return;
        f->epoch = g_resetEpoch;

        bool inconsistent = false;
        for (auto& s : f->ps) {
            if (s.delayedRelease || s.releaseNext || !s.waveQueue.empty()) {
                inconsistent = true;
            }
            s = PState();
        }

        // Wave Grid / Min Gap are skipped briefly after a spawn so the first
        // clicks go straight through.
        f->graceUntil = f->step + spawnGraceSteps();

        // We may have held back a release the game never saw. Releasing a
        // button that is not held is harmless, so make sure nothing is stuck.
        if (inconsistent) {
            if (m_player1) m_player1->releaseButton(PlayerButton::Jump);
            if (m_player2) m_player2->releaseButton(PlayerButton::Jump);
        }
        DBG("[InputBuffer] state reset (death/restart), spawn grace until step {}{}",
            f->graceUntil, inconsistent ? ", released stuck button" : "");
    }

    void flushWaveQueue(PlayerObject* p, PState& s, int idx) {
        for (auto const& ev : s.waveQueue) {
            sendInput(p, idx, ev.first, false);
        }
        s.waveQueue.clear();
    }

    void stepPlayer(PlayerObject* p, PState& s, int idx) {
        int step = m_fields->step;
        char const* tag = idx == 0 ? "P1" : "P2";

        if (!s.waveQueue.empty()) {
            if (!p->m_isDart) {
                flushWaveQueue(p, s, idx);
            } else {
                size_t applied = 0;
                while (applied < s.waveQueue.size() &&
                       step >= s.waveQueue[applied].second) {
                    auto ev = s.waveQueue[applied];
                    sendInput(p, idx, ev.first, false);
                    DBG("[InputBuffer] {} WAVE {} {} applied at step {}",
                        tag, waveModeName(), ev.first ? "PRESS" : "RELEASE", step);
                    applied++;
                }
                s.waveQueue.erase(s.waveQueue.begin(), s.waveQueue.begin() + applied);
            }
        }

        if (s.delayedRelease) {
            if (!isFlyMode(p) || step >= s.releaseAt) {
                s.delayedRelease = false;
                s.held = false;
                sendInput(p, idx, false, false);
                DBG("[InputBuffer] {} MIN-HOLD release applied at step {} [{}]",
                    tag, step, modeName(p));
            }
        }

        if (s.releaseNext) {
            s.releaseNext = false;
            if (!s.held) sendInput(p, idx, false, true);
        }

        if (!s.pending) return;

        int waited = step - s.pendingStart;
        int limit = modeLimitSteps(p);

        if (limit <= 0 || waited > limit || !isGroundMode(p)) {
            DBG("[InputBuffer] {} pending EXPIRED waited={} limit={} mode={}",
                tag, waited, limit, modeName(p));
            s.pending = false;
            return;
        }

        int rings = ringsTouching(p);
        if (rings != s.lastRingCount) {
            DBG("[InputBuffer] {} ring overlap count {} -> {} (waited {})",
                tag, s.lastRingCount, rings, waited);
            s.lastRingCount = rings;
        }

        bool onGround = p->m_isOnGround;
        bool onRing = rings > 0;

        if (onGround || onRing) {
            s.pending = false;
            float ticksSaved = waited / stepsPerTick();
            char const* what = onRing ? "RING" : "GROUND";

            if (s.held) {
                DBG("[InputBuffer] {} reached {} after {} steps but button still held, skipping ({})",
                    tag, what, waited, modeName(p));
            } else {
                sendInput(p, idx, true, true);
                s.releaseNext = true;
                ++g_savedCount;
                announce(fmt::format(
                    "[InputBuffer] *** SAVED +{:.1f} TICKS ({:.2f} frames @ 60fps) PRESS! [{} {} {}] (Step0: {} -> Delayed: {}) [Limit: {} steps] ***",
                    ticksSaved, ticksSaved / 4.0f, tag, modeName(p), what,
                    s.pendingStart, step, limit));
                // v2.0.0: ademas de consola y log, notificacion en pantalla —
                // ver el SAVED sin abrir el log de Geode. (API de Geode 5.x:
                // Notification::create, NotificationBuilder ya no existe.)
                Notification::create(
                    fmt::format("Assisted Click: SAVED +{:.1f} ticks ({} {})",
                                ticksSaved, modeName(p), what),
                    NotificationIcon::Success
                )->show();
            }
        }
    }

    bool inputPlayer(PlayerObject* p, PState& s, int idx, bool down) {
        int step = m_fields->step;
        char const* tag = idx == 0 ? "P1" : "P2";

        if (!p->m_isDart && !s.waveQueue.empty()) {
            flushWaveQueue(p, s, idx);
        }

        int n = waveGridSteps();
        bool inGrace = step < m_fields->graceUntil;
        if (p->m_isDart && inGrace && n > 0 && g_waveMode != WAVE_HOLD) {
            DBG("[InputBuffer] {} WAVE {} {} passed through (spawn grace) at step {}",
                tag, waveModeName(), down ? "PRESS" : "RELEASE", step);
        }

        if (p->m_isDart && n > 0 && g_waveMode != WAVE_HOLD && !inGrace) {
            int target;
            if (g_waveMode == WAVE_GRID) {
                target = ((step + n - 1) / n) * n;
                if (!s.waveQueue.empty()) {
                    auto const& last = s.waveQueue.back();
                    target = std::max(target, last.second);
                    // A tap must hold for at least one grid cell, or the
                    // press and release would cancel out in the same step.
                    if (!down && last.first) {
                        target = std::max(target, last.second + n);
                    }
                }
            } else { // WAVE_GAP
                target = step;
                if (!s.waveQueue.empty()) {
                    target = std::max(target, s.waveQueue.back().second);
                }
                if (down) {
                    target = std::max(target, s.lastReleaseStep + n);
                } else {
                    s.lastReleaseStep = target;
                }
                if (target == step && s.waveQueue.empty()) {
                    s.held = down;
                    return false;
                }
            }

            s.held = down;
            s.waveQueue.emplace_back(down, target);
            DBG("[InputBuffer] {} WAVE {} {} queued at step {} -> {} ({} steps)",
                tag, waveModeName(), down ? "PRESS" : "RELEASE", step, target, n);
            return true;
        }

        if (down && s.delayedRelease) {
            s.delayedRelease = false;
            s.held = false;
            sendInput(p, idx, false, false);
        }

        if (!down && isFlyMode(p) && s.pressStep >= 0) {
            int minHold = modeLimitSteps(p);
            int heldFor = step - s.pressStep;
            if (minHold > 0 && heldFor < minHold) {
                s.delayedRelease = true;
                s.releaseAt = s.pressStep + minHold;
                DBG("[InputBuffer] {} MIN-HOLD delaying release: held {} < {} steps [{}]",
                    tag, heldFor, minHold, modeName(p));
                return true;
            }
        }

        s.held = down;

        if (down) {
            s.pressStep = isFlyMode(p) ? step : -1;

            bool early = isGroundMode(p) && !p->m_isOnGround && ringsTouching(p) == 0;
            if (early && modeLimitSteps(p) > 0) {
                s.pending = true;
                s.pendingStart = step;
                s.lastRingCount = 0;
            } else {
                s.pending = false;
            }
        }
        return false;
    }

    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        auto f = m_fields.operator->();
        f->step++;
        pollKeys();
        syncState();

        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);

        if (!PlayLayer::get()) {
            for (auto& s : f->ps) s = PState();
            return;
        }

        if (m_player1) stepPlayer(m_player1, f->ps[0], 0);
        if (m_player2) stepPlayer(m_player2, f->ps[1], 1);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        auto f = m_fields.operator->();
        syncState();

        if (PlayLayer::get() && button == 1) {
            int idx = isPlayer1 ? 0 : 1;
            auto p = isPlayer1 ? m_player1 : m_player2;
            if (p) {
                if (inputPlayer(p, f->ps[idx], idx, down)) return;
            }
        }

        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};
