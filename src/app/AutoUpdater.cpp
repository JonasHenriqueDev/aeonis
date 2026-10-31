#include "AutoUpdater.h"
#include "core/Log.h"
#include "core/Paths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace aeonis {

AutoUpdater::AutoUpdater(QObject *parent) : QObject(parent)
{
    m_status = QStringLiteral("Versão %1").arg(version());
    connect(&m_timer, &QTimer::timeout, this, [this] {
        m_timer.setInterval(24 * 3600 * 1000);
        check();
    });
}

QString AutoUpdater::version() const { return QStringLiteral(AEONIS_VERSION); }

void AutoUpdater::setAutomatic(bool enabled)
{
    if (enabled && !m_timer.isActive()) {
        m_timer.setInterval(3 * 1000); // o suficiente para a bandeja e a rede subirem
        m_timer.start();
    } else if (!enabled) {
        m_timer.stop();
    }
}

namespace {

QString attemptFile() { return QDir(Paths::appData()).filePath(QStringLiteral("update-attempt.txt")); }

} // namespace

bool AutoUpdater::recentlyTried(const QString &version) const
{
    QFile f(attemptFile());
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QStringList parts = QString::fromUtf8(f.readAll()).trimmed().split(QLatin1Char('|'));
    if (parts.size() != 2 || parts[0] != version)
        return false;
    const QDateTime at = QDateTime::fromString(parts[1], Qt::ISODate);
    return at.isValid() && at.secsTo(QDateTime::currentDateTimeUtc()) < 24 * 3600;
}

bool AutoUpdater::installIncomplete() const
{
    const QDir dir(QCoreApplication::applicationDirPath());
    if (!QFile::exists(dir.filePath(QStringLiteral("unins000.exe"))))
        return false;
    const QSettings marker(dir.filePath(QStringLiteral("install.ini")), QSettings::IniFormat);
    return marker.value(QStringLiteral("Install/Version")).toString() != version();
}

void AutoUpdater::fail(const QString &reason)
{
    m_progress = -1;
    setStatus(reason, false);
    emit installFailed(reason);
}

void AutoUpdater::setStatus(const QString &status, bool busy)
{
    m_status = status;
    m_busy = busy;
    emit changed();
}

int AutoUpdater::compareVersions(const QString &a, const QString &b)
{
    auto split = [](QString v, QString *pre) {
        if (v.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
            v.remove(0, 1);
        const int dash = v.indexOf(QLatin1Char('-'));
        *pre = dash >= 0 ? v.mid(dash + 1) : QString();
        if (dash >= 0)
            v.truncate(dash);
        QList<int> parts;
        for (const QString &p : v.split(QLatin1Char('.')))
            parts << p.toInt();
        while (parts.size() < 3)
            parts << 0;
        return parts;
    };
    QString preA, preB;
    const QList<int> pa = split(a, &preA), pb = split(b, &preB);
    for (int i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        const int x = pa.value(i), y = pb.value(i);
        if (x != y)
            return x < y ? -1 : 1;
    }
    if (preA.isEmpty() != preB.isEmpty())
        return preA.isEmpty() ? 1 : -1;
    return QString::compare(preA, preB);
}

void AutoUpdater::check()
{
    if (m_busy)
        return;
    setStatus(QStringLiteral("Verificando atualizações…"), true);
    QNetworkRequest req(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(QLatin1String(Repository))));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Aeonis/%1").arg(version()));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(15000);
    m_reply = m_net.get(req);
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply.data()] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            Log::warn(QStringLiteral("Verificação de atualizações: %1").arg(reply->errorString()));
            // Sem rede (ex.: logo depois de ligar o PC): tenta de novo sozinho em alguns minutos.
            static const int retryMinutes[] = {1, 5, 15};
            const bool offline = reply->error() < QNetworkReply::ContentAccessDenied; // erros de conexão/DNS/tempo
            if (offline && m_timer.isActive() && m_retries < 3)
                m_timer.setInterval(retryMinutes[m_retries++] * 60 * 1000);
            setStatus(offline ? QStringLiteral("Sem conexão com o GitHub agora. O Aeonis tenta de novo sozinho.")
                              : QStringLiteral("Não foi possível verificar atualizações (%1).").arg(reply->errorString()),
                      false);
            return;
        }
        m_retries = 0;
        const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = release.value(QLatin1String("tag_name")).toString();
        static const QRegularExpression setup(QStringLiteral(R"(^Aeonis-Setup-[\w.\-]+\.exe$)"), QRegularExpression::CaseInsensitiveOption);
        QString url, name;
        for (const QJsonValue &v : release.value(QLatin1String("assets")).toArray()) {
            const QJsonObject asset = v.toObject();
            if (setup.match(asset.value(QLatin1String("name")).toString()).hasMatch()) {
                url = asset.value(QLatin1String("browser_download_url")).toString();
                name = asset.value(QLatin1String("name")).toString();
                m_assetSize = asset.value(QLatin1String("size")).toInteger();
                break;
            }
        }
        const int newer = compareVersions(tag, version());
        if (tag.isEmpty() || newer < 0 || (newer == 0 && !installIncomplete()) || url.isEmpty()) {
            m_assetUrl.clear();
            setStatus(QStringLiteral("Você está na versão mais recente (%1).").arg(version()), false);
            return;
        }
        m_newVersion = tag.startsWith(QLatin1Char('v')) ? tag.mid(1) : tag;
        m_assetUrl = url;
        m_assetName = name;
        setStatus(QStringLiteral("Versão %1 disponível.").arg(m_newVersion), false);
        if (!m_notified) {
            m_notified = true;
            emit updateFound(m_newVersion);
        }
    });
}

QString AutoUpdater::installerPath() const
{
    return QDir::toNativeSeparators(QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QLatin1Char('/') + m_assetName);
}

bool AutoUpdater::ready() const
{
    if (m_assetName.isEmpty())
        return false;
    QFile f(installerPath());
    if (!f.open(QIODevice::ReadOnly) || f.size() < 1'000'000 || (m_assetSize > 0 && f.size() != m_assetSize))
        return false;
    return f.read(2) == "MZ";
}

void AutoUpdater::prefetch()
{
    if (m_busy || m_assetUrl.isEmpty())
        return;
    if (ready()) {
        emit downloaded();
        return;
    }
    download(false, false);
}

void AutoUpdater::downloadAndInstall(bool reopenWindow)
{
    if (m_assetUrl.isEmpty())
        return;
    if (ready()) {
        emit installStarted();
        launch(reopenWindow);
        return;
    }
    if (m_busy)
        return;
    emit installStarted(); // pedido do usuário: mostra a tela de atualização já no download
    download(true, reopenWindow);
}

void AutoUpdater::download(bool installAfter, bool reopenWindow)
{
    const QUrl url(m_assetUrl);
    if (url.host() != QLatin1String("github.com") && !url.host().endsWith(QLatin1String(".github.com"))) {
        setStatus(QStringLiteral("Endereço de download inesperado."), false);
        return;
    }
    m_progress = 0;
    setStatus(QStringLiteral("Baixando a versão %1…").arg(m_newVersion), true);
    Log::info(QStringLiteral("Atualização: baixando %1 (%2)").arg(m_assetName, installAfter ? QStringLiteral("para instalar") : QStringLiteral("em segundo plano")));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Aeonis/%1").arg(version()));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(120000);
    // Grava direto no disco (.part) enquanto baixa; só vira o instalador quando completo.
    auto *part = new QFile(installerPath() + QStringLiteral(".part"), this);
    if (!part->open(QIODevice::WriteOnly)) {
        delete part;
        installAfter ? fail(QStringLiteral("Não foi possível salvar o instalador.")) : setStatus(QStringLiteral("Não foi possível salvar o instalador."), false);
        return;
    }
    QElapsedTimer *clock = new QElapsedTimer;
    clock->start();
    m_reply = m_net.get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, [part, reply = m_reply.data()] { part->write(reply->readAll()); });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        if (total > 0) {
            m_progress = qreal(got) / total;
            m_status = QStringLiteral("Baixando a versão %1… %2%").arg(m_newVersion).arg(got * 100 / total);
            emit changed();
        }
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, installAfter, reopenWindow, part, clock, reply = m_reply.data()] {
        reply->deleteLater();
        part->write(reply->readAll());
        part->close();
        const qint64 ms = clock->elapsed();
        delete clock;
        const QString partPath = part->fileName();
        part->deleteLater();
        auto failed = [&](const QString &why) {
            QFile::remove(partPath);
            Log::warn(QStringLiteral("Atualização: %1").arg(why));
            if (installAfter)
                fail(why);
            else {
                m_progress = -1;
                setStatus(why, false);
            }
        };
        if (reply->error() != QNetworkReply::NoError)
            return failed(QStringLiteral("Falha no download (%1).").arg(reply->errorString()));
        QFile check(partPath);
        if (!check.open(QIODevice::ReadOnly) || check.size() < 1'000'000 || check.read(2) != "MZ"
            || (m_assetSize > 0 && check.size() != m_assetSize))
            return failed(QStringLiteral("O instalador baixado é inválido."));
        check.close();
        QFile::remove(installerPath());
        if (!QFile::rename(partPath, installerPath()))
            return failed(QStringLiteral("Não foi possível salvar o instalador."));
        Log::info(QStringLiteral("Atualização: %1 baixado em %2 s.").arg(m_assetName).arg(ms / 1000.0, 0, 'f', 1));
        m_progress = -1;
        if (installAfter) {
            launch(reopenWindow);
        } else {
            setStatus(QStringLiteral("Versão %1 pronta para instalar.").arg(m_newVersion), false);
            emit downloaded();
        }
    });
}

void AutoUpdater::launch(bool reopenWindow)
{
    const QString path = installerPath();
    Log::info(QStringLiteral("Atualização: iniciando %1").arg(path));
    QFile attempt(attemptFile());
    if (attempt.open(QIODevice::WriteOnly))
        attempt.write(QStringLiteral("%1|%2").arg(m_newVersion, QDateTime::currentDateTimeUtc().toString(Qt::ISODate)).toUtf8());
    m_progress = -1;
    setStatus(QStringLiteral("Instalando a versão %1…").arg(m_newVersion), true);
    // /VERYSILENT: sem a janela do Inno Setup; o progresso aparece na tela de atualização do app.
    if (!QProcess::startDetached(path, {QStringLiteral("/VERYSILENT"), QStringLiteral("/SUPPRESSMSGBOXES"),
                                        QStringLiteral("/NORESTART"), QStringLiteral("/UPDATE=1"),
                                        QStringLiteral("/OPENWINDOW=%1").arg(reopenWindow ? 1 : 0)})) {
        fail(QStringLiteral("Não foi possível iniciar o instalador."));
        return;
    }
    emit installerLaunched();
}

} // namespace aeonis
