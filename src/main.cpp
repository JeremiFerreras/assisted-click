#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/CCKeyboardDispatcher.hpp>
#include <Geode/ui/Notification.hpp>
#include <deque>
#include <algorithm>

using namespace geode::prelude;

namespace {
    // Unidades que avanza el jugador por tick (medido en los logs)
    constexpr float kPasoX = 1.2983f;

    struct Pendiente {
        int aplicarEnTick;
        bool down;
        int button;
        bool isPlayer1;
        int tickClick;
        float xClick;
    };

    std::deque<Pendiente> g_cola;
    int g_tick = 0;            // llamadas a processCommands desde el reinicio
    int g_ultimoAplicar = 0;   // para no desordenar clicks si cambia el retraso
    bool g_aplicando = false;  // true mientras APLICAMOS un click guardado
    int g_logRestantes = 24;   // limite de lineas de log por cada valor de F6

    const int kOpciones[] = {0, 1, 2, 4, 8};
    int g_indice = 0;

    int retrasoActual() { return kOpciones[g_indice]; }
}

class $modify(MiTeclado, CCKeyboardDispatcher) {
    bool dispatchKeyboardMSG(enumKeyCodes key, bool down, bool repeat, double timestamp) {
        if (key == enumKeyCodes::KEY_F6 && down && !repeat) {
            g_indice = (g_indice + 1) % (sizeof(kOpciones) / sizeof(kOpciones[0]));
            g_logRestantes = 24;
            int d = retrasoActual();
            log::info("[AC] F6 -> retraso = {} ticks", d);
            Notification::create(fmt::format("Retraso: {} ticks", d),
                NotificationIcon::None, 1.0f)->show();
        }
        return CCKeyboardDispatcher::dispatchKeyboardMSG(key, down, repeat, timestamp);
    }
};

class $modify(MiPlayLayer, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        g_cola.clear();
        g_tick = 0;
        g_ultimoAplicar = 0;
    }
};

class $modify(MiBaseLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        if (PlayLayer::get()) {
            g_tick++;
            // Aplicar los clicks cuyo momento ya llego, antes de este tick
            while (!g_cola.empty() && g_cola.front().aplicarEnTick <= g_tick) {
                Pendiente p = g_cola.front();
                g_cola.pop_front();
                if (m_player1 && g_logRestantes > 0) {
                    g_logRestantes--;
                    float movido = m_player1->getPositionX() - p.xClick;
                    log::info("[AC] APLICADO {} llamada#{} | esperó {} ticks fisicos (x movida {:.3f})",
                        p.down ? "ABAJO" : "ARRIBA", g_tick,
                        std::lround(movido / kPasoX), movido);
                }
                // La marca evita que este click vuelva a entrar en nuestro retraso
                g_aplicando = true;
                GJBaseGameLayer::handleButton(p.down, p.button, p.isPlayer1);
                g_aplicando = false;
            }
        }
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        int d = retrasoActual();
        // Pasar tal cual: click aplicado por nosotros, sin retraso, inicio de nivel o fuera de nivel
        if (g_aplicando || d == 0 || g_tick == 0 || !PlayLayer::get() || !m_player1) {
            GJBaseGameLayer::handleButton(down, button, isPlayer1);
            return;
        }
        int cuando = std::max(g_tick + d, g_ultimoAplicar);
        g_ultimoAplicar = cuando;
        g_cola.push_back(Pendiente{cuando, down, button, isPlayer1,
            g_tick, m_player1->getPositionX()});
        if (g_logRestantes > 0) {
            g_logRestantes--;
            log::info("[AC] GUARDADO {} retraso={} llamada#{} x={:.3f}",
                down ? "ABAJO" : "ARRIBA", d, g_tick, m_player1->getPositionX());
        }
    }
};
