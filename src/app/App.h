#pragma once
// O aplicativo de bandeja (porta do TrayApp.cs): detecta jogos, liga/desliga a captura, salva clipes
// e destaques, registra o histórico e mostra avisos. Sem a janela aberta, o Aeonis é só isto.
//
// Toda operação da captura (iniciar, parar, clipe, gravação) roda numa fila serial fora da UI, na
// ordem em que foi pedida: um destaque pendente é salvo antes de a sessão ser encerrada.

#include "capture/CaptureEngine.h"
#include "core/Settings.h"
#include "games/GameCatalog.h"

#include <QDateTime>
#include <QObject>
#include <QSystemTrayIcon>
#include <QThreadPool>
#include <QVariantMap>

#include <functional>
#include <memory>
#include <optional>

namespace aeonis {

class AutoUpdater;
class Backend;
class EventSource;
class GameWatcher;
class Highlighter;
class Hotkeys;
class TrayIcon;
class WindowHost;

class App : public QObject {
    Q_OBJECT
public:
    explicit App(bool minimized, QObject *parent = nullptr);
    ~App() override;

    const Settings &settings() const { return m_settings; }
    /// Salva, aplica (atalhos, inicialização, limite) e avisa a interface.
    void applySettings(const Settings &updated);
    QVariantMap status() const;
    AutoUpdater *updater() const { return m_updater; }

    void showWindow(const QString &page);
    /// Encerramento do Windows: para a captura sem gerar a gravação (recuperada na próxima abertura).
    void emergencyStop();

public slots:
    void saveManualClip();
    void toggleRecording();
    void openOutputFolder();
    void openLog();
    void exit();

signals:
    void statusChanged();
    void libraryChanged();
    void toast(const QString &text, const QString &kind);

private:
    using Done = std::function<void()>;

    /// Enfileira `work` (na fila da captura) e roda `done` na UI com o resultado.
    template <typename Work, typename Then>
    void enqueue(Work work, Then then);

    void startEngine(const QString &game, HMONITOR monitor, const QString &exe, std::function<void(bool)> then);
    void stopEngineAndReport(Done then);
    void saveClip(int seconds, const QString &label, const QString &kind);
    void onGameStarted(const GameDef &game, HMONITOR monitor, const QString &exe);
    void onGameStopped(const GameDef &game);
    void startDesktopBufferIfWanted();
    void registerHotkeys();
    void enforceQuota();
    void updateUi();
    void notify(const QString &title, const QString &text, QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information);
    static QString labelOf(const QString &path);

    Settings m_settings;
    CaptureEngine m_engine;
    QThreadPool m_ops; // 1 thread: operações da captura em ordem
    Highlighter *m_highlighter = nullptr;
    GameWatcher *m_watcher = nullptr;
    std::unique_ptr<Hotkeys> m_hotkeys;
    TrayIcon *m_tray = nullptr;
    EventSource *m_events = nullptr;
    AutoUpdater *m_updater = nullptr;
    Backend *m_backend = nullptr;
    WindowHost *m_window = nullptr;
    std::optional<GameDef> m_activeGame;
    bool m_starting = false;
    bool m_exiting = false;
};

} // namespace aeonis
