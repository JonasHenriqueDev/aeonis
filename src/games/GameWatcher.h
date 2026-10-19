#pragma once
// Detecta jogos pelo nome do processo: uma enumeração (CreateToolhelp32Snapshot) a cada 3 s enquanto
// nenhum jogo roda; depois só verifica o PID. Espera a janela do jogo aparecer (até 20 s) para
// descobrir em qual monitor ele está.

#include "GameCatalog.h"

#include <QObject>
#include <QTimer>

#include <functional>
#include <optional>

#include <windows.h>

namespace aeonis {

class GameWatcher : public QObject {
    Q_OBJECT
public:
    explicit GameWatcher(std::function<QList<GameDef>()> games, QObject *parent = nullptr);

    void start();
    void stop();
    std::optional<GameDef> current() const { return m_current; }

signals:
    void gameStarted(const aeonis::GameDef &game, HMONITOR monitor, const QString &exe);
    void gameStopped(const aeonis::GameDef &game);

private:
    void tick();
    static bool isAlive(DWORD pid, const QString &name);

    std::function<QList<GameDef>()> m_games;
    QTimer m_timer;
    std::optional<GameDef> m_current, m_pending;
    DWORD m_currentPid = 0, m_pendingPid = 0;
    QString m_currentName, m_pendingName;
    qint64 m_pendingSince = 0;
};

} // namespace aeonis
