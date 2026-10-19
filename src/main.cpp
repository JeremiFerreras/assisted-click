#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>

using namespace geode::prelude;

namespace {
    int g_calls = 0;    // llamadas a processCommands desde el ultimo reinicio
    int g_pattern = 0;  // cuantas llamadas hemos anotado para ver el patron
}

class $modify(MiPlayLayer, PlayLayer) {
    void resetLevel() {
        PlayLayer::resetLevel();
        g_calls = 0;
        g_pattern = 0;
        log::info("[AC] resetLevel: contador a 0");
    }
};

class $modify(MiBaseLayer, GJBaseGameLayer) {
    void processCommands(float dt, bool isHalfTick, bool isLastTick) {
        GJBaseGameLayer::processCommands(dt, isHalfTick, isLastTick);
        if (!PlayLayer::get()) return;

        g_calls++;
        // Solo anotamos las primeras 30 llamadas para ver el patron sin llenar el log
        if (g_pattern < 30) {
            g_pattern++;
            log::info("[AC] processCommands #{} dt={:.6f} half={} last={}",
                g_calls, dt, isHalfTick, isLastTick);
        }
    }

    void handleButton(bool down, int button, bool isPlayer1) {
        if (PlayLayer::get() && m_player1) {
            log::info("[AC] BOTON {} button={} p1={} llamada#{} x={:.3f} y={:.3f}",
                down ? "ABAJO" : "ARRIBA", button, isPlayer1, g_calls,
                m_player1->getPositionX(), m_player1->getPositionY());
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
};
