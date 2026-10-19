#include "App.h"
#include "AutoUpdater.h"
#include "Hotkeys.h"
#include "TrayIcon.h"
#include "UpdateSplash.h"
#include "WindowHost.h"
#include "capture/Clips.h"
#include "capture/Monitors.h"
#include "core/Async.h"
#include "core/Library.h"
#include "core/Log.h"
#include "core/MediaCache.h"
#include "core/Paths.h"
#include "games/EventSources.h"
#include "games/GameWatcher.h"
#include "games/Highlighter.h"
#include "share/YouTube.h"
#include "ui/Backend.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>
#include <QUrl>

namespace aeonis {

template <typename Work, typename Then>
void App::enqueue(Work work, Then then)
{
    runAsync(this, std::move(work), std::move(then), &m_ops);
}

App::App(bool minimized, QObject *parent) : QObject(parent), m_settings(Settings::load())
{
    m_ops.setMaxThreadCount(1);
    m_ops.setExpiryTimeout(-1);

    connect(&m_engine, &CaptureEngine::failed, this, [this](const QString &message) {
        notify(QStringLiteral("Aeonis"), message, QSystemTrayIcon::Critical);
        stopEngineAndReport({});
    });

    m_highlighter = new Highlighter([this] { return std::make_pair(m_settings.clipPreSeconds, m_settings.clipPostSeconds); },
                                    [this](int seconds, const QString &label) { saveClip(seconds, label, QStringLiteral("highlight")); },
                                    this);
    m_watcher = new GameWatcher([this] { return GameCatalog::all(m_settings.customGames); }, this);
    connect(m_watcher, &GameWatcher::gameStarted, this, &App::onGameStarted);
    connect(m_watcher, &GameWatcher::gameStopped, this, &App::onGameStopped);

    m_tray = new TrayIcon(this);
    connect(m_tray, &TrayIcon::openRequested, this, [this] { showWindow(QStringLiteral("matches")); });
    connect(m_tray, &TrayIcon::settingsRequested, this, [this] { showWindow(QStringLiteral("settings")); });
    connect(m_tray, &TrayIcon::saveClipRequested, this, &App::saveManualClip);
    connect(m_tray, &TrayIcon::toggleRecordingRequested, this, &App::toggleRecording);
    connect(m_tray, &TrayIcon::openFolderRequested, this, &App::openOutputFolder);
    connect(m_tray, &TrayIcon::openLogRequested, this, &App::openLog);
    connect(m_tray, &TrayIcon::quitRequested, this, &App::exit);

    m_hotkeys = std::make_unique<Hotkeys>();
    registerHotkeys();

    m_updater = new AutoUpdater(this);
    m_updater->setAutomatic(m_settings.autoUpdate);
    connect(m_updater, &AutoUpdater::updateFound, this, [this](const QString &version) {
        if (m_settings.autoUpdate) {
            installUpdateIfIdle();
            return;
        }
        notify(QStringLiteral("Atualização disponível"),
               QStringLiteral("O Aeonis %1 está disponível. Abra Sobre para instalar.").arg(version));
    });
    connect(m_updater, &AutoUpdater::installerLaunched, this, &App::exit);
    // Tela de atualização (logo + progresso) no lugar da janela enquanto baixa e instala.
    m_splash = new UpdateSplash(m_updater, this);
    connect(m_updater, &AutoUpdater::installStarted, this, [this] {
        m_windowBeforeUpdate = m_window->isOpen();
        m_window->close();
        m_splash->show();
    });
    connect(m_updater, &AutoUpdater::installFailed, this, [this](const QString &reason) {
        m_splash->close();
        notify(QStringLiteral("Falha na atualização"), reason, QSystemTrayIcon::Critical);
        if (m_windowBeforeUpdate)
            showWindow(QStringLiteral("about"));
    });

    // Envio ao YouTube: vive no App para continuar com a janela fechada. YouTubeClientId/Secret no
    // settings.json trocam o cliente OAuth embutido (para quem usa um projeto próprio no Google Cloud).
    m_youtube = new YouTube(m_settings.extra.value(QStringLiteral("YouTubeClientId")).toString(),
                            m_settings.extra.value(QStringLiteral("YouTubeClientSecret")).toString(), this);
    connect(m_youtube, &YouTube::connectFinished, this, [this](bool ok, const QString &message) {
        if (!message.isEmpty())
            emit toast(message, ok ? QStringLiteral("info") : QStringLiteral("error"));
    });
    connect(m_youtube, &YouTube::uploadFinished, this, [this](bool ok, const QString &path, const QString &url, const QString &message) {
        emit toast(message, ok ? QStringLiteral("info") : QStringLiteral("error"));
        if (!m_window->isOpen())
            notify(ok ? QStringLiteral("Enviado para o YouTube") : QStringLiteral("Envio para o YouTube"),
                   ok ? QStringLiteral("%1\n%2").arg(QFileInfo(path).fileName(), url) : message,
                   ok ? QSystemTrayIcon::Information : QSystemTrayIcon::Critical);
    });

    m_backend = new Backend(this, this);
    m_window = new WindowHost(m_backend, this);

    // Primeiro recupera gravações interrompidas; a captura só começa depois (fila serial).
    enqueue([] {
        QList<QPair<ClipResult, QString>> recovered;
        CaptureEngine::recoverLeftovers([&](const ClipResult &r, const QString &game) { recovered.append({r, game}); });
        return recovered;
    }, [this](QList<QPair<ClipResult, QString>> recovered) {
        for (const auto &[r, game] : recovered) {
            Library::clipSaved(QString(), r.path, QStringLiteral("full"), QStringLiteral("Recuperado"),
                               QDateTime::currentDateTime(), r.duration);
            notify(QStringLiteral("Gravação recuperada"), QFileInfo(r.path).fileName());
        }
        if (!recovered.isEmpty())
            emit libraryChanged();
        enforceQuota();
        startDesktopBufferIfWanted();
    });

    m_watcher->start();
    updateUi();
    // Aberto pelo usuário (atalho/menu Iniciar): mostra a janela. Com o Windows (--minimized): só a bandeja.
    if (!minimized)
        showWindow(QStringLiteral("matches"));
}

App::~App()
{
    delete m_window; // a janela usa o Backend: sai antes dele
    m_window = nullptr;
}

void App::notify(const QString &title, const QString &text, QSystemTrayIcon::MessageIcon icon)
{
    if (m_settings.notifications || icon == QSystemTrayIcon::Critical)
        m_tray->notify(title, text, icon);
}

QString App::labelOf(const QString &path)
{
    const QString name = QFileInfo(path).completeBaseName();
    return name.contains(QStringLiteral("_Gravação")) ? QStringLiteral("Gravação")
         : name.contains(QStringLiteral("_Recuperado")) ? QStringLiteral("Recuperado")
                                                        : QStringLiteral("Partida");
}

// ---------------------------------------------------------------- ciclo de vida da captura

void App::startEngine(const QString &game, HMONITOR monitor, const QString &exe, std::function<void(bool)> then)
{
    const Settings s = m_settings;
    enqueue([this, game, monitor, s] {
        QString err;
        const bool already = m_engine.running();
        const bool ok = m_engine.start(game, monitor, s, &err);
        return std::make_tuple(ok, already, err);
    }, [this, game, exe, then](std::tuple<bool, bool, QString> r) {
        const auto &[ok, already, err] = r;
        if (ok && !already) {
            Library::sessionStarted(m_engine.sessionId(), game, exe);
            if (!exe.isEmpty())
                runAsync(this, [game, exe] { MediaCache::saveGameIcon(game, exe); }, [] {});
        } else if (!ok) {
            Log::error(QStringLiteral("Falha ao iniciar captura: %1").arg(err));
            notify(QStringLiteral("Falha ao iniciar a captura"), err, QSystemTrayIcon::Critical);
        }
        updateUi();
        if (then)
            then(ok);
    });
}

void App::startDesktopBufferIfWanted()
{
    if (m_settings.alwaysOnBuffer && !m_exiting && !m_activeGame && !m_engine.running())
        startEngine(QStringLiteral("Desktop"), Monitors::byIndex(std::max(0, m_settings.monitorIndex)), {}, {});
}

void App::installUpdateIfIdle()
{
    // Atualiza sozinho ao abrir, mas nunca no meio de uma partida ou gravação;
    // nesse caso tenta de novo quando o jogo fechar ou a gravação terminar.
    if (!m_settings.autoUpdate || m_exiting || !m_updater->available() || m_updater->busy()
        || m_activeGame || m_engine.recording())
        return;
    if (m_updater->recentlyTried(m_updater->newVersion())) {
        Log::error(QStringLiteral("Atualização para %1 já foi tentada e o app continua na %2; não tenta de novo sozinho.")
                       .arg(m_updater->newVersion(), m_updater->version()));
        return;
    }
    Log::info(QStringLiteral("Atualização automática para %1").arg(m_updater->newVersion()));
    m_updater->downloadAndInstall(m_window->isOpen());
}

void App::onGameStarted(const GameDef &game, HMONITOR monitor, const QString &exe)
{
    m_activeGame = game;
    auto start = [this, game, monitor, exe] {
        const HMONITOR target = m_settings.monitorIndex >= 0 ? Monitors::byIndex(m_settings.monitorIndex) : monitor;
        startEngine(game.name, target, exe, [this, game, exe](bool ok) {
            if (!ok)
                return;
            if (m_settings.mode != CaptureMode::Destaques)
                enqueue([this] { m_engine.beginRecording(); }, [this] { updateUi(); });
            if (m_settings.autoHighlights && !m_events) {
                auto options = [this] { return EventOptions{m_settings.clipDeaths, m_settings.clipAssists}; };
                if (game.provider == QLatin1String("lol"))
                    m_events = new LeagueEvents(options, exe, this);
                else if (game.provider == QLatin1String("cs2"))
                    m_events = new Cs2Events(options, exe, [this](const QString &t, const QString &m) { notify(t, m); }, this);
                if (m_events) {
                    connect(m_events, &EventSource::highlight, this, [this](const QString &reason) {
                        Library::event(m_engine.sessionId(), reason); // marcador na linha do tempo
                        if (m_settings.mode != CaptureMode::SomentePartida)
                            m_highlighter->trigger(reason);
                    });
                    connect(m_events, &EventSource::marker, this, [this](const QString &label) {
                        Library::event(m_engine.sessionId(), label);
                    });
                    connect(m_events, &EventSource::matchInfo, this, [this](const QJsonObject &info) {
                        Library::matchInfo(m_engine.sessionId(), info);
                    });
                    // LoL: a gravação começa com o processo (tela de carregamento) e é adiantada
                    // para o 0:00 do relógio quando a partida de fato começa.
                    connect(m_events, &EventSource::clockStarted, this, [this](double gameTime) {
                        enqueue([this, gameTime] { m_engine.moveRecordingStart(gameTime); }, [this] { updateUi(); });
                    });
                    m_events->start();
                }
            }
            const QString autoText = m_events ? QStringLiteral(" Destaques automáticos ativos.") : QString();
            notify(QStringLiteral("%1 detectado").arg(game.name),
                   QStringLiteral("Gravando com %1. %2 salva um clipe.%3")
                       .arg(m_engine.encoderLabel(), m_settings.hotkeySaveClip, autoText));
        });
    };
    if (m_engine.running())
        stopEngineAndReport([start](bool) { start(); });
    else
        start();
}

void App::onGameStopped(const GameDef &)
{
    if (m_events) {
        m_events->deleteLater();
        m_events = nullptr;
    }
    m_highlighter->flush(); // entra na fila antes do encerramento
    const QString session = m_engine.sessionId();
    stopEngineAndReport([this, session](bool savedRecording) {
        m_activeGame.reset();
        // Abre a partida para análise (se algo dela foi salvo).
        if (m_settings.openMatchOnEnd && !m_exiting && !session.isEmpty()
            && (savedRecording || m_lastClipSession == session))
            showWindow(QStringLiteral("match:") + session);
        startDesktopBufferIfWanted();
        updateUi();
        installUpdateIfIdle();
    });
}

void App::stopEngineAndReport(Done then)
{
    struct Stopped {
        ClipResult recording;
        QString session;
        QDateTime recordingStart;
        bool wasRunning = false;
    };
    const QDateTime requestedAt = QDateTime::currentDateTime();
    enqueue([this] {
        Stopped s;
        s.session = m_engine.sessionId();
        s.recordingStart = m_engine.recordingSince();
        s.wasRunning = m_engine.running();
        s.recording = m_engine.stop();
        return s;
    }, [this, requestedAt, then = std::move(then)](Stopped s) {
        if (s.recording) {
            Library::clipSaved(s.session, s.recording.path, QStringLiteral("full"), labelOf(s.recording.path), requestedAt,
                               s.recording.duration, s.recordingStart);
            notify(QStringLiteral("Partida salva"), QFileInfo(s.recording.path).fileName());
            enforceQuota();
        }
        if (s.wasRunning)
            Library::sessionEnded(s.session);
        emit libraryChanged();
        updateUi();
        if (then)
            then(bool(s.recording));
    });
}

void App::saveManualClip()
{
    if (!m_engine.running()) {
        notify(QStringLiteral("Nada sendo gravado"),
               QStringLiteral("Abra um jogo ou ative \"Buffer sempre ativo\" nas configurações."), QSystemTrayIcon::Warning);
        return;
    }
    saveClip(m_settings.replaySeconds, QStringLiteral("Clipe"), QStringLiteral("manual"));
}

void App::saveClip(int seconds, const QString &label, const QString &kind)
{
    const QDateTime requestedAt = QDateTime::currentDateTime();
    enqueue([this, seconds, label] { return std::make_pair(m_engine.sessionId(), m_engine.saveClip(seconds, label)); },
            [this, label, kind, requestedAt](std::pair<QString, ClipResult> r) {
        if (!r.second) {
            notify(QStringLiteral("Falha ao salvar clipe"), QStringLiteral("Veja o log para detalhes."), QSystemTrayIcon::Critical);
            return;
        }
        Library::clipSaved(r.first, r.second.path, kind, label, requestedAt, r.second.duration);
        m_lastClipSession = r.first;
        notify(kind == QLatin1String("manual") ? QStringLiteral("Clipe salvo") : QStringLiteral("Destaque salvo: %1").arg(label),
               QFileInfo(r.second.path).fileName());
        enforceQuota();
        emit libraryChanged();
    });
}

void App::toggleRecording()
{
    auto begin = [this] {
        enqueue([this] { m_engine.beginRecording(); }, [this] {
            notify(QStringLiteral("Gravação iniciada"), QStringLiteral("%1 para parar.").arg(m_settings.hotkeyToggleRecording));
            updateUi();
        });
    };
    if (!m_engine.running()) {
        const QString game = m_activeGame ? m_activeGame->name : QStringLiteral("Desktop");
        startEngine(game, Monitors::byIndex(std::max(0, m_settings.monitorIndex)), {}, [begin](bool ok) {
            if (ok)
                begin();
        });
        return;
    }
    if (!m_engine.recording()) {
        begin();
        return;
    }
    struct Ended {
        QString session;
        QDateTime start;
        ClipResult result;
    };
    const QDateTime requestedAt = QDateTime::currentDateTime();
    enqueue([this] {
        Ended e;
        e.session = m_engine.sessionId();
        e.start = m_engine.recordingSince();
        e.result = m_engine.endRecording();
        return e;
    }, [this, requestedAt](Ended e) {
        if (e.result) {
            Library::clipSaved(e.session, e.result.path, QStringLiteral("full"), labelOf(e.result.path), requestedAt,
                               e.result.duration, e.start);
            notify(QStringLiteral("Gravação salva"), QFileInfo(e.result.path).fileName());
            enforceQuota();
        } else {
            notify(QStringLiteral("Falha ao salvar gravação"), QStringLiteral("Veja o log para detalhes."), QSystemTrayIcon::Critical);
        }
        emit libraryChanged();
        if (!m_activeGame && !m_settings.alwaysOnBuffer)
            stopEngineAndReport({});
        updateUi();
        installUpdateIfIdle();
    });
}

void App::enforceQuota()
{
    const QString dir = m_settings.outputDir;
    const double gb = m_settings.maxStorageGb;
    if (gb <= 0)
        return;
    runAsync(this, [dir, gb] { return Clips::enforceQuota(dir, gb); }, [this](int removed) {
        if (removed > 0)
            emit libraryChanged();
    });
}

// ---------------------------------------------------------------- interface

QVariantMap App::status() const
{
    const bool running = m_engine.running();
    const bool recording = running && m_engine.recording();
    QVariantMap m;
    m.insert(QStringLiteral("state"), !running ? QStringLiteral("idle") : recording ? QStringLiteral("recording") : QStringLiteral("buffer"));
    m.insert(QStringLiteral("game"), running ? m_engine.game() : QString());
    m.insert(QStringLiteral("encoder"), running ? m_engine.encoderLabel() : QString());
    m.insert(QStringLiteral("hotkeySave"), m_settings.hotkeySaveClip);
    m.insert(QStringLiteral("hotkeyRecord"), m_settings.hotkeyToggleRecording);
    m.insert(QStringLiteral("version"), QStringLiteral(AEONIS_VERSION));
    m.insert(QStringLiteral("recordingSince"), recording ? m_engine.recordingSince() : QDateTime());
    return m;
}

void App::updateUi()
{
    const bool running = m_engine.running();
    const bool recording = running && m_engine.recording();
    const QString game = m_engine.game();
    TrayIcon::State s;
    s.status = !running ? QStringLiteral("Aguardando jogo…")
             : recording ? QStringLiteral("● Gravando: %1").arg(game)
                         : QStringLiteral("Buffer ativo: %1").arg(game);
    s.canSave = running;
    s.recording = recording;
    s.hotkeySave = m_settings.hotkeySaveClip;
    s.hotkeyRecord = m_settings.hotkeyToggleRecording;
    m_tray->update(s);
    emit statusChanged();
}

void App::registerHotkeys()
{
    m_hotkeys->clear();
    QStringList errors;
    QString err;
    if (!m_hotkeys->add(m_settings.hotkeySaveClip, [this] { saveManualClip(); }, &err))
        errors << err;
    if (!m_hotkeys->add(m_settings.hotkeyToggleRecording, [this] { toggleRecording(); }, &err))
        errors << err;
    if (!errors.isEmpty())
        notify(QStringLiteral("Atalhos"), errors.join(QLatin1Char('\n')), QSystemTrayIcon::Warning);
}

void App::applySettings(const Settings &updated)
{
    m_settings = updated;
    if (!m_settings.save())
        notify(QStringLiteral("Erro ao salvar configurações"), Paths::settingsFile(), QSystemTrayIcon::Critical);
    Settings::applyStartup(m_settings.startWithWindows);
    registerHotkeys();
    m_engine.updateSettings(m_settings);
    m_updater->setAutomatic(m_settings.autoUpdate);
    enforceQuota();
    startDesktopBufferIfWanted();
    updateUi();
    emit libraryChanged();
}

void App::showWindow(const QString &page) { m_window->show(page); }

void App::openOutputFolder()
{
    QDir().mkpath(m_settings.outputDir);
    QProcess::startDetached(QStringLiteral("explorer.exe"), {QDir::toNativeSeparators(m_settings.outputDir)});
}

void App::openLog()
{
    if (QFileInfo::exists(Paths::logFile()))
        QDesktopServices::openUrl(QUrl::fromLocalFile(Paths::logFile()));
}

void App::emergencyStop()
{
    m_watcher->stop();
    m_engine.stopForShutdown();
}

void App::exit()
{
    if (m_exiting)
        return;
    m_exiting = true;
    m_watcher->stop();
    if (m_events) {
        m_events->deleteLater();
        m_events = nullptr;
    }
    m_highlighter->flush();
    m_window->close();
    stopEngineAndReport([this](bool) {
        m_tray->hide();
        m_ops.waitForDone();
        QCoreApplication::quit();
    });
}

} // namespace aeonis
