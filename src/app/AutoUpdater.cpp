#include "AutoUpdater.h"
#include "core/Log.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
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
            setStatus(QStringLiteral("Não foi possível verificar atualizações (%1).").arg(reply->errorString()), false);
            return;
        }
        const QJsonObject release = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = release.value(QLatin1String("tag_name")).toString();
        static const QRegularExpression setup(QStringLiteral(R"(^Aeonis-Setup-[\w.\-]+\.exe$)"), QRegularExpression::CaseInsensitiveOption);
        QString url, name;
        for (const QJsonValue &v : release.value(QLatin1String("assets")).toArray()) {
            const QJsonObject asset = v.toObject();
            if (setup.match(asset.value(QLatin1String("name")).toString()).hasMatch()) {
                url = asset.value(QLatin1String("browser_download_url")).toString();
                name = asset.value(QLatin1String("name")).toString();
                break;
            }
        }
        if (tag.isEmpty() || compareVersions(tag, version()) <= 0 || url.isEmpty()) {
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

void AutoUpdater::downloadAndInstall(bool reopenWindow)
{
    if (m_busy || m_assetUrl.isEmpty())
        return;
    const QUrl url(m_assetUrl);
    if (url.host() != QLatin1String("github.com") && !url.host().endsWith(QLatin1String(".github.com"))) {
        setStatus(QStringLiteral("Endereço de download inesperado."), false);
        return;
    }
    setStatus(QStringLiteral("Baixando a versão %1…").arg(m_newVersion), true);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Aeonis/%1").arg(version()));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(120000);
    m_reply = m_net.get(req);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        if (total > 0) {
            m_status = QStringLiteral("Baixando a versão %1… %2%").arg(m_newVersion).arg(got * 100 / total);
            emit changed();
        }
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, reopenWindow, reply = m_reply.data()] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setStatus(QStringLiteral("Falha no download (%1).").arg(reply->errorString()), false);
            return;
        }
        const QByteArray data = reply->readAll();
        if (data.size() < 1'000'000 || !data.startsWith("MZ")) {
            setStatus(QStringLiteral("O instalador baixado é inválido."), false);
            return;
        }
        const QString path = QDir::toNativeSeparators(QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                                                      + QLatin1Char('/') + m_assetName);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
            setStatus(QStringLiteral("Não foi possível salvar o instalador."), false);
            return;
        }
        f.close();
        Log::info(QStringLiteral("Atualização: iniciando %1").arg(path));
        if (!QProcess::startDetached(path, {QStringLiteral("/SILENT"), QStringLiteral("/SUPPRESSMSGBOXES"),
                                            QStringLiteral("/NORESTART"), QStringLiteral("/UPDATE=1"),
                                            QStringLiteral("/OPENWINDOW=%1").arg(reopenWindow ? 1 : 0)})) {
            setStatus(QStringLiteral("Não foi possível iniciar o instalador."), false);
            return;
        }
        setStatus(QStringLiteral("Instalando a versão %1…").arg(m_newVersion), true);
        emit installerLaunched();
    });
}

} // namespace aeonis
