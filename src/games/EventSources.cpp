#include "EventSources.h"
#include "core/Log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslError>
#include <QTcpServer>
#include <QTcpSocket>

namespace aeonis {

// ---------------------------------------------------------------- League of Legends

LeagueEvents::LeagueEvents(Options options, QObject *parent) : EventSource(std::move(options), parent)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &LeagueEvents::poll);
}

void LeagueEvents::start()
{
    m_net = new QNetworkAccessManager(this);
    m_timer.start();
}

void LeagueEvents::poll()
{
    if (m_busy)
        return;
    m_busy = true;
    auto get = [this](const char *path) {
        QNetworkRequest req(QUrl(QStringLiteral("https://127.0.0.1:2999/liveclientdata/") + QLatin1String(path)));
        req.setTransferTimeout(2000);
        QNetworkReply *reply = m_net->get(req);
        // A API usa um certificado autoassinado da Riot; o endereço é fixo em loopback.
        connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &) { reply->ignoreSslErrors(); });
        return reply;
    };
    if (m_me.isEmpty()) {
        QNetworkReply *r = get("activeplayername");
        connect(r, &QNetworkReply::finished, this, [this, r] {
            r->deleteLater();
            if (r->error() == QNetworkReply::NoError) {
                const QByteArray body = r->readAll().trimmed();
                const QJsonDocument doc = QJsonDocument::fromJson("[" + body + "]");
                m_me = doc.array().at(0).toString();
            }
            m_busy = false;
        });
        return;
    }
    QNetworkReply *r = get("eventdata");
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        m_busy = false;
        if (r->error() != QNetworkReply::NoError) {
            m_me.clear(); // tela de carregamento ou partida terminando
            return;
        }
        handleEvents(r->readAll());
    });
}

void LeagueEvents::handleEvents(const QByteArray &body)
{
    const QJsonArray events = QJsonDocument::fromJson(body).object().value(QLatin1String("Events")).toArray();
    int maxId = -1;
    for (const QJsonValue &v : events)
        maxId = std::max(maxId, v.toObject().value(QLatin1String("EventID")).toInt());
    if (m_lastId == INT_MIN || maxId < m_lastId) {
        m_lastId = maxId; // linha de base: ignora o histórico (aberto no meio da partida / nova partida)
        return;
    }
    const EventOptions opts = m_options();
    for (const QJsonValue &v : events) {
        const QJsonObject e = v.toObject();
        const int id = e.value(QLatin1String("EventID")).toInt();
        if (id <= m_lastId)
            continue;
        m_lastId = id;
        const QString hit = classify(e, m_me, opts);
        if (!hit.isEmpty())
            emit highlight(hit);
    }
}

QString LeagueEvents::classify(const QJsonObject &e, const QString &me, const EventOptions &options)
{
    const QString meShort = me.section(QLatin1Char('#'), 0, 0);
    auto isMe = [&](const QString &n) {
        return !n.isEmpty() && !me.isEmpty()
            && (n.compare(me, Qt::CaseInsensitive) == 0 || n.compare(meShort, Qt::CaseInsensitive) == 0);
    };
    auto str = [&](const char *key) { return e.value(QLatin1String(key)).toString(); };
    const QString name = str("EventName");
    if (name == QLatin1String("ChampionKill")) {
        if (isMe(str("KillerName")))
            return QStringLiteral("Abate");
        if (options.deaths && isMe(str("VictimName")))
            return QStringLiteral("Morte");
        if (options.assists)
            for (const QJsonValue &a : e.value(QLatin1String("Assisters")).toArray())
                if (isMe(a.toString()))
                    return QStringLiteral("Assistência");
        return {};
    }
    if (name == QLatin1String("Multikill") && isMe(str("KillerName"))) {
        switch (e.value(QLatin1String("KillStreak")).toInt()) {
        case 2: return QStringLiteral("Double Kill");
        case 3: return QStringLiteral("Triple Kill");
        case 4: return QStringLiteral("Quadra Kill");
        default: return QStringLiteral("Penta Kill");
        }
    }
    if (name == QLatin1String("Ace") && isMe(str("Acer")))
        return QStringLiteral("Ace");
    static const QList<QPair<const char *, const char *>> objectives = {
        {"DragonKill", "Dragão"}, {"BaronKill", "Barão"}, {"HeraldKill", "Arauto"},
        {"TurretKilled", "Torre"}, {"InhibKilled", "Inibidor"},
    };
    for (const auto &[event, label] : objectives)
        if (name == QLatin1String(event) && isMe(str("KillerName")))
            return QString::fromUtf8(label);
    return {};
}

// ---------------------------------------------------------------- Counter-Strike 2

Cs2Events::Cs2Events(Options options, const QString &exePath, std::function<void(QString, QString)> notify, QObject *parent)
    : EventSource(std::move(options), parent)
{
    if (ensureConfig(exePath) && notify)
        notify(QStringLiteral("CS2: destaques automáticos ativados"),
               QStringLiteral("Reinicie o CS2 uma vez para os abates serem detectados."));
}

bool Cs2Events::ensureConfig(const QString &exePath)
{
    if (exePath.isEmpty())
        return false;
    // ...\Counter-Strike Global Offensive\game\bin\win64\cs2.exe -> ...\game\csgo\cfg
    QDir gameDir = QFileInfo(exePath).absoluteDir();
    if (!gameDir.cdUp() || !gameDir.cdUp())
        return false;
    const QString cfgDir = gameDir.absoluteFilePath(QStringLiteral("csgo/cfg"));
    if (!QFileInfo(cfgDir).isDir())
        return false;
    const QString path = QDir::toNativeSeparators(cfgDir + QStringLiteral("/gamestate_integration_aeonis.cfg"));
    if (QFileInfo::exists(path))
        return false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        Log::error(QStringLiteral("Instalação do GSI do CS2: não foi possível criar %1").arg(path));
        return false;
    }
    f.write(QStringLiteral(
        "\"Aeonis\"\n"
        "{\n"
        "    \"uri\" \"http://127.0.0.1:%1/\"\n"
        "    \"timeout\" \"1.0\"\n"
        "    \"buffer\" \"0.1\"\n"
        "    \"throttle\" \"0.25\"\n"
        "    \"heartbeat\" \"30.0\"\n"
        "    \"auth\" { \"token\" \"%2\" }\n"
        "    \"data\"\n"
        "    {\n"
        "        \"provider\" \"1\"\n"
        "        \"map\" \"1\"\n"
        "        \"player_id\" \"1\"\n"
        "        \"player_state\" \"1\"\n"
        "        \"player_match_stats\" \"1\"\n"
        "    }\n"
        "}\n").arg(Port).arg(QLatin1String(Token)).toUtf8());
    Log::info(QStringLiteral("CS2 GSI instalado em %1").arg(path));
    return true;
}

void Cs2Events::start()
{
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost, Port)) {
        Log::error(QStringLiteral("Porta %1 indisponível para o GSI do CS2: %2").arg(Port).arg(m_server->errorString()));
        return;
    }
    connect(m_server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket *s = m_server->nextPendingConnection()) {
            auto *buffer = new QByteArray;
            QTimer::singleShot(5000, s, [s] { s->abort(); });
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            connect(s, &QObject::destroyed, [buffer] { delete buffer; });
            connect(s, &QTcpSocket::readyRead, this, [this, s, buffer] {
                buffer->append(s->readAll());
                if (buffer->size() > (2 << 20)) {
                    s->abort();
                    return;
                }
                const int headerEnd = buffer->indexOf("\r\n\r\n");
                if (headerEnd < 0)
                    return;
                int length = 0;
                for (const QByteArray &line : buffer->left(headerEnd).split('\n'))
                    if (line.trimmed().toLower().startsWith("content-length:"))
                        length = line.mid(line.indexOf(':') + 1).trimmed().toInt();
                if (buffer->size() < headerEnd + 4 + length)
                    return;
                const QByteArray body = buffer->mid(headerEnd + 4, length);
                s->write("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                s->disconnectFromHost();
                buffer->clear();
                const QJsonDocument doc = QJsonDocument::fromJson(body);
                if (doc.isObject())
                    process(doc.object());
            });
        }
    });
}

void Cs2Events::process(const QJsonObject &root)
{
    if (root.value(QLatin1String("auth")).toObject().value(QLatin1String("token")).toString() != QLatin1String(Token))
        return;
    const QString map = root.value(QLatin1String("map")).toObject().value(QLatin1String("name")).toString();
    if (map != m_lastMap) {
        m_lastMap = map;
        m_lastKills.reset();
        m_lastDeaths.reset();
    }
    // Só o jogador local: morto e assistindo um aliado, o "player" é outra pessoa.
    const QString provider = root.value(QLatin1String("provider")).toObject().value(QLatin1String("steamid")).toString();
    const QJsonObject player = root.value(QLatin1String("player")).toObject();
    if (player.isEmpty() || provider.isEmpty() || player.value(QLatin1String("steamid")).toString() != provider)
        return;
    const QJsonValue statsValue = player.value(QLatin1String("match_stats"));
    if (!statsValue.isObject())
        return;
    const QJsonObject stats = statsValue.toObject();
    const int kills = stats.value(QLatin1String("kills")).toInt();
    const int deaths = stats.value(QLatin1String("deaths")).toInt();
    const int roundKills = player.value(QLatin1String("state")).toObject().value(QLatin1String("round_kills")).toInt();
    if (m_lastKills && kills > *m_lastKills) {
        emit highlight(roundKills >= 5 ? QStringLiteral("Ace")
                       : roundKills == 4 ? QStringLiteral("Quadra Kill")
                       : roundKills == 3 ? QStringLiteral("Triple Kill")
                       : roundKills == 2 ? QStringLiteral("Double Kill")
                                         : QStringLiteral("Abate"));
    }
    if (m_options().deaths && m_lastDeaths && deaths > *m_lastDeaths)
        emit highlight(QStringLiteral("Morte"));
    m_lastKills = kills;
    m_lastDeaths = deaths;
}

} // namespace aeonis
