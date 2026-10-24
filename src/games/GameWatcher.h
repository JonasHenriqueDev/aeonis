#pragma once
// Detecta jogos pelo nome do processo: uma enumeração (CreateToolhelp32Snapshot) a cada 3 s enquanto
// nenhum jogo roda; depois só verifica o PID. Espera a janela do jogo aparecer (até 20 s) para
// descobrir em qual monitor ele está. Se a janela do jogo some e o processo fica preso em segundo plano
// (fechou mas não saiu), a sessão termina mesmo assim depois de um minuto.

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
    bool m_hadWindow = false;   // o jogo atual já mostrou janela
    qint64 m_noWindowSince = 0; // desde quando o jogo atual está sem janela
    DWORD m_ignoredPid = 0;     // processo encerrado por falta de janela: não é detectado de novo
};

} // namespace aeonis
