// Aeonis — gravador de gameplay leve, open-source e sem anúncios (Qt 6 + FFmpeg).
//
//   Aeonis.exe [--minimized]
//   Aeonis.exe --selftest [--out pasta] [--encoder Auto|NvidiaH264|...] [--no-audio] [--mic] [--seconds N]
//
// --minimized: aberto pelo Windows (só o ícone na bandeja).
// --selftest:  grava, salva um clipe, faz uma gravação longa e escreve %LOCALAPPDATA%\Aeonis\selftest.txt.

#include "app/App.h"
#include "app/AppIcon.h"
#include "capture/CaptureEngine.h"
#include "capture/Monitors.h"
#include "core/Log.h"
#include "core/Media.h"
#include "core/Paths.h"
#include "core/Settings.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQuickStyle>
#include <QSessionManager>
#include <QStyleHints>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>

#include <windows.h>
#include <shobjidl.h>

using namespace aeonis;

namespace {

QString arg(const QStringList &args, const QString &name)
{
    const int i = args.indexOf(name);
    return i >= 0 && i + 1 < args.size() ? args[i + 1] : QString();
}

int selfTest(const QStringList &args)
{
    QStringList report;
    int code = 1;
    Settings s;
    if (const QString out = arg(args, QStringLiteral("--out")); !out.isEmpty())
        s.outputDir = QDir::toNativeSeparators(QFileInfo(out).absoluteFilePath());
    if (const QString enc = arg(args, QStringLiteral("--encoder")); !enc.isEmpty())
        s.encoder = encoderChoiceFrom(enc);
    if (args.contains(QStringLiteral("--no-audio")))
        s.systemAudio = false;
    if (args.contains(QStringLiteral("--mic")))
        s.microphone = true;
    bool ok = false;
    int seconds = arg(args, QStringLiteral("--seconds")).toInt(&ok);
    if (!ok || seconds <= 0)
        seconds = 12;

    CaptureEngine engine;
    QString err;
    if (!engine.start(QStringLiteral("SelfTest"), Monitors::byIndex(0), s, &err)) {
        report << QStringLiteral("ERRO: %1").arg(err);
    } else {
        report << QStringLiteral("encoder: %1").arg(engine.encoderLabel());
        QThread::msleep(2000);
        engine.beginRecording();
        QThread::msleep(DWORD(seconds) * 1000);
        const ClipResult clip = engine.saveClip(6, QStringLiteral("Teste"));
        report << QStringLiteral("clipe: %1").arg(clip ? clip.path : QStringLiteral("(falhou)"));
        const ClipResult rec = engine.stop();
        report << QStringLiteral("gravação: %1").arg(rec ? rec.path : QStringLiteral("(falhou)"));
        for (const ClipResult &f : {clip, rec})
            if (f)
                report << QStringLiteral("  %1: %2 KiB, %3s").arg(QFileInfo(f.path).fileName())
                              .arg(QFileInfo(f.path).size() / 1024).arg(Media::duration(f.path), 0, 'f', 1);
        code = clip && rec ? 0 : 1;
    }
    report << (code == 0 ? QStringLiteral("OK") : QStringLiteral("FALHOU"));
    QFile f(Paths::appData() + QStringLiteral("/selftest.txt"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
        f.write(report.join(QLatin1Char('\n')).toUtf8() + '\n');
    return code;
}

} // namespace

int main(int argc, char *argv[])
{
    Paths::ensure();
    Log::rotate();
    Log::installHandlers();

    QStringList rawArgs;
    for (int i = 1; i < argc; ++i)
        rawArgs << QString::fromLocal8Bit(argv[i]);

    if (rawArgs.contains(QStringLiteral("--selftest"))) {
        // A Desktop Duplication (DuplicateOutput1) exige processo ciente de DPI por monitor.
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        QCoreApplication app(argc, argv);
        const int code = selfTest(QCoreApplication::arguments());
        return code;
    }

    // O instalador segura este mutex enquanto roda: não abre no meio de uma atualização.
    if (!rawArgs.contains(QStringLiteral("--after-update"))) {
        if (HANDLE setup = OpenMutexW(SYNCHRONIZE, FALSE, L"AeonisSetupMutex")) {
            CloseHandle(setup);
            return 0;
        }
    }

    // Identidade própria na barra de tarefas (a mesma dos atalhos do instalador): o Windows não reaproveita
    // o ícone em cache da versão antiga, que usava o mesmo Aeonis.exe.
    SetCurrentProcessExplicitAppUserModelID(L"Aeonis.Aeonis");

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Aeonis"));
    QCoreApplication::setOrganizationName(QStringLiteral("Aeonis"));
    QCoreApplication::setApplicationVersion(QStringLiteral(AEONIS_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setWindowIcon(AppIcon::create(false));
    QQuickStyle::setStyle(QStringLiteral("FluentWinUI3"));
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);

    // Uma instância por usuário: abrir de novo (atalho, menu Iniciar) só mostra a janela.
    const QString instanceName = QStringLiteral("Aeonis-%1").arg(qEnvironmentVariable("USERNAME"));
    {
        QLocalSocket other;
        other.connectToServer(instanceName);
        if (other.waitForConnected(500)) {
            AllowSetForegroundWindow(ASFW_ANY);
            other.write(rawArgs.contains(QStringLiteral("--minimized")) ? "tray" : "show");
            other.waitForBytesWritten(500);
            other.disconnectFromServer();
            return 0;
        }
    }
    // Mesmo nome da versão C#: o instalador (AppMutex) sabe que o Aeonis está aberto.
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Aeonis.SingleInstance");

    Log::info(QStringLiteral("Aeonis %1 iniciado.").arg(QStringLiteral(AEONIS_VERSION)));
    App aeonisApp(rawArgs.contains(QStringLiteral("--minimized")));

    QLocalServer instanceServer;
    QLocalServer::removeServer(instanceName);
    instanceServer.listen(instanceName);
    QObject::connect(&instanceServer, &QLocalServer::newConnection, &aeonisApp, [&] {
        while (QLocalSocket *client = instanceServer.nextPendingConnection()) {
            QObject::connect(client, &QLocalSocket::disconnected, client, &QObject::deleteLater);
            QObject::connect(client, &QLocalSocket::readyRead, &aeonisApp, [&aeonisApp, client] {
                if (client->readAll().startsWith("show"))
                    aeonisApp.showWindow(QStringLiteral("matches"));
            });
        }
    });

    // Logoff/desligamento: fecha os segmentos; a gravação longa é recuperada na próxima abertura.
    QObject::connect(&app, &QGuiApplication::commitDataRequest, &aeonisApp, [&aeonisApp](QSessionManager &) {
        aeonisApp.emergencyStop();
    });

    // Depuração: --ui-screenshot arquivo.png [--open página] salva a janela e encerra.
    if (const QString shot = arg(rawArgs, QStringLiteral("--ui-screenshot")); !shot.isEmpty()) {
        const QString page = arg(rawArgs, QStringLiteral("--open"));
        aeonisApp.showWindow(page.isEmpty() ? QStringLiteral("matches") : page);
        QTimer::singleShot(6000, &aeonisApp, [shot] {
            for (QWindow *w : QGuiApplication::topLevelWindows())
                if (auto *q = qobject_cast<QQuickWindow *>(w); q && q->isVisible())
                    q->grabWindow().save(shot);
            QCoreApplication::exit(0);
        });
    }

    const int code = app.exec();
    aeonisApp.emergencyStop();
    if (mutex)
        CloseHandle(mutex);
    Log::info(QStringLiteral("Aeonis encerrado."));
    return code;
}
