#include "EventSources.h"
#include "core/Log.h"
#include "core/MediaCache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslError>
#include <QTcpServer>
#include <QTcpSocket>

#include <cmath>

namespace aeonis {

// ---------------------------------------------------------------- League of Legends

namespace {

QString championIdFromRaw(const QString &raw)
{
    // "game_character_displayname_MonkeyKing" -> "MonkeyKing" (id do Data Dragon).
    const int i = raw.lastIndexOf(QLatin1Char('_'));
    return i >= 0 ? raw.mid(i + 1) : raw;
}

bool sameName(const QString &a, const QString &me)
{
    const QString meShort = me.section(QLatin1Char('#'), 0, 0);
    return !a.isEmpty() && !me.isEmpty()
        && (a.compare(me, Qt::CaseInsensitive) == 0 || a.compare(meShort, Qt::CaseInsensitive) == 0
            || a.section(QLatin1Char('#'), 0, 0).compare(meShort, Qt::CaseInsensitive) == 0);
}

QString playerName(const QJsonObject &p)
{
    for (const char *key : {"riotId", "summonerName", "riotIdGameName"}) {
        const QString v = p.value(QLatin1String(key)).toString();
        if (!v.isEmpty())
            return v;
    }
    return {};
}

} // namespace

LeagueEvents::LeagueEvents(Options options, const QString &exePath, QObject *parent)
    : EventSource(std::move(options), parent), m_exe(exePath)
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
    // /allgamedata (eventos + jogadores + relógio) até o relógio começar e a cada 5 s; no resto, só os eventos.
    const bool all = m_me.isEmpty() || !m_clock || ++m_tick % 5 == 0;
    QNetworkRequest req(QUrl(QStringLiteral("https://127.0.0.1:2999/liveclientdata/")
                             + (all ? QStringLiteral("allgamedata") : QStringLiteral("eventdata"))));
    req.setTransferTimeout(2000);
    QNetworkReply *r = m_net->get(req);
    // A API usa um certificado autoassinado da Riot; o endereço é fixo em loopback.
    connect(r, &QNetworkReply::sslErrors, r, [r](const QList<QSslError> &) { r->ignoreSslErrors(); });
    connect(r, &QNetworkReply::finished, this, [this, r, all] {
        r->deleteLater();
        m_busy = false;
        if (r->error() != QNetworkReply::NoError) {
            m_me.clear(); // tela de carregamento ou partida terminando
            return;
        }
        if (all)
            handleAll(r->readAll());
        else
            handleEvents(QJsonDocument::fromJson(r->readAll()).object().value(QLatin1String("Events")).toArray());
    });
}

void LeagueEvents::handleAll(const QByteArray &body)
{
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    const QString me = playerName(root.value(QLatin1String("activePlayer")).toObject());
    if (me.isEmpty())
        return; // ainda carregando
    m_me = me;
    const double gameTime = root.value(QLatin1String("gameData")).toObject().value(QLatin1String("gameTime")).toDouble();
    if (!m_clock && gameTime > 0.5) {
        m_clock = true;
        m_info.insert(QStringLiteral("gameStart"),
                      QDateTime::currentDateTime().addMSecs(-qint64(gameTime * 1000)).toString(Qt::ISODateWithMs));
        Log::info(QStringLiteral("LoL: relógio da partida em %1 s.").arg(gameTime, 0, 'f', 1));
        emit clockStarted(gameTime);
        fetchQueue();
    }
    const QJsonArray players = root.value(QLatin1String("allPlayers")).toArray();
    m_team.clear();
    m_allies.clear();
    for (const QJsonValue &v : players)
        if (sameName(playerName(v.toObject()), m_me))
            m_team = v.toObject().value(QLatin1String("team")).toString();
    for (const QJsonValue &v : players) {
        const QJsonObject p = v.toObject();
        if (m_team.isEmpty() || p.value(QLatin1String("team")).toString() != m_team)
            continue;
        for (const char *key : {"riotId", "summonerName", "riotIdGameName", "championName"})
            if (!p.value(QLatin1String(key)).toString().isEmpty())
                m_allies << p.value(QLatin1String(key)).toString();
    }
    const QJsonObject player = playerInfo(root, m_me);
    for (auto it = player.begin(); it != player.end(); ++it)
        m_info.insert(it.key(), it.value());
    if (gameTime > 0)
        m_info.insert(QStringLiteral("gameTime"), std::round(gameTime));
    if (!m_iconRequested && !player.value(QLatin1String("championId")).toString().isEmpty()) {
        m_iconRequested = true;
        fetchChampionIcon(player.value(QLatin1String("championId")).toString());
    }
    handleEvents(root.value(QLatin1String("events")).toObject().value(QLatin1String("Events")).toArray());
    publish();
}

void LeagueEvents::publish()
{
    if (m_info != m_published && m_info.contains(QLatin1String("champion"))) {
        m_published = m_info;
        emit matchInfo(m_info);
    }
}

void LeagueEvents::handleEvents(const QJsonArray &events)
{
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
        if (e.value(QLatin1String("EventName")).toString() == QLatin1String("GameEnd")) {
            const QString result = e.value(QLatin1String("Result")).toString();
            if (!result.isEmpty())
                m_info.insert(QStringLiteral("result"), result);
            publish();
            continue;
        }
        const QString hit = classify(e, m_me, opts);
        const QString mark = timelineLabel(e, m_me, m_team, m_allies);
        if (!hit.isEmpty())
            emit highlight(hit);
        else if (!mark.isEmpty())
            emit marker(mark);
    }
}

void LeagueEvents::fetchQueue()
{
    // A fila (ranqueada, normal...) vem do cliente (LCU): ...\League of Legends\lockfile =
    // "LeagueClient:pid:porta:senha:https". Sem ele, fica o modo da partida (gameMode).
    QDir dir = QFileInfo(m_exe).absoluteDir();
    QFile lock;
    for (int up = 0; up < 3; ++up) {
        lock.setFileName(dir.absoluteFilePath(QStringLiteral("lockfile")));
        if (lock.open(QIODevice::ReadOnly) || !dir.cdUp())
            break;
    }
    if (!lock.isOpen())
        return;
    const QList<QByteArray> parts = lock.readAll().trimmed().split(':');
    if (parts.size() < 5)
        return;
    QNetworkRequest req(QUrl(QStringLiteral("https://127.0.0.1:%1/lol-gameflow/v1/session").arg(QString::fromLatin1(parts[2]))));
    req.setRawHeader("Authorization", "Basic " + QByteArray("riot:" + parts[3]).toBase64());
    req.setTransferTimeout(3000);
    QNetworkReply *r = m_net->get(req);
    connect(r, &QNetworkReply::sslErrors, r, [r](const QList<QSslError> &) { r->ignoreSslErrors(); });
    connect(r, &QNetworkReply::finished, this, [this, r] {
        r->deleteLater();
        const QJsonObject queue = QJsonDocument::fromJson(r->readAll()).object()
                                      .value(QLatin1String("gameData")).toObject().value(QLatin1String("queue")).toObject();
        QString name = queueName(queue.value(QLatin1String("id")).toInt(-1));
        if (name.isEmpty())
            name = queue.value(QLatin1String("description")).toString();
        if (!name.isEmpty()) {
            m_info.insert(QStringLiteral("queue"), name);
            publish();
        }
    });
}

void LeagueEvents::fetchChampionIcon(const QString &championId)
{
    const QString path = MediaCache::championIconPath(championId);
    if (QFileInfo::exists(path))
        return;
    // Ícone do campeão no Data Dragon (CDN oficial da Riot), na versão mais recente.
    QNetworkRequest req(QUrl(QStringLiteral("https://ddragon.leagueoflegends.com/api/versions.json")));
    req.setTransferTimeout(10000);
    QNetworkReply *r = m_net->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, championId, path] {
        r->deleteLater();
        const QString version = QJsonDocument::fromJson(r->readAll()).array().at(0).toString();
        if (version.isEmpty())
            return;
        QNetworkRequest img(QUrl(QStringLiteral("https://ddragon.leagueoflegends.com/cdn/%1/img/champion/%2.png").arg(version, championId)));
        img.setTransferTimeout(10000);
        QNetworkReply *ri = m_net->get(img);
        connect(ri, &QNetworkReply::finished, this, [ri, path] {
            ri->deleteLater();
            const QByteArray png = ri->readAll();
            if (ri->error() != QNetworkReply::NoError || !png.startsWith("\x89PNG"))
                return;
            QFile f(path);
            if (f.open(QIODevice::WriteOnly))
                f.write(png);
        });
    });
}

QJsonObject LeagueEvents::playerInfo(const QJsonObject &root, const QString &me)
{
    QJsonObject info;
    const QString mode = root.value(QLatin1String("gameData")).toObject().value(QLatin1String("gameMode")).toString();
    if (!mode.isEmpty())
        info.insert(QStringLiteral("mode"), modeName(mode));
    for (const QJsonValue &v : root.value(QLatin1String("allPlayers")).toArray()) {
        const QJsonObject p = v.toObject();
        if (!sameName(playerName(p), me))
            continue;
        const QJsonObject scores = p.value(QLatin1String("scores")).toObject();
        info.insert(QStringLiteral("champion"), p.value(QLatin1String("championName")).toString());
        info.insert(QStringLiteral("championId"), championIdFromRaw(p.value(QLatin1String("rawChampionName")).toString()));
        info.insert(QStringLiteral("kills"), scores.value(QLatin1String("kills")).toInt());
        info.insert(QStringLiteral("deaths"), scores.value(QLatin1String("deaths")).toInt());
        info.insert(QStringLiteral("assists"), scores.value(QLatin1String("assists")).toInt());
        info.insert(QStringLiteral("cs"), scores.value(QLatin1String("creepScore")).toInt());
        info.insert(QStringLiteral("level"), p.value(QLatin1String("level")).toInt());
        break;
    }
    return info;
}

QString LeagueEvents::queueName(int id)
{
    switch (id) {
    case 0: return QStringLiteral("Personalizada");
    case 400: return QStringLiteral("Normal (Escolha Alternada)");
    case 420: return QStringLiteral("Ranqueada Solo/Duo");
    case 430: return QStringLiteral("Normal (Às Cegas)");
    case 440: return QStringLiteral("Ranqueada Flexível");
    case 450: return QStringLiteral("ARAM");
    case 480: case 490: return QStringLiteral("Partida Rápida");
    case 700: return QStringLiteral("Clash");
    case 720: return QStringLiteral("ARAM Clash");
    case 830: case 840: case 850: case 870: case 880: case 890: return QStringLiteral("Contra IA");
    case 900: case 1010: return QStringLiteral("ARURF");
    case 1020: return QStringLiteral("Um Por Todos");
    case 1300: return QStringLiteral("Nexus Blitz");
    case 1400: return QStringLiteral("Livro de Feitiços Supremo");
    case 1700: case 1710: return QStringLiteral("Arena");
    case 1900: return QStringLiteral("URF");
    default: return {};
    }
}

QString LeagueEvents::modeName(const QString &mode)
{
    static const QHash<QString, QString> names = {
        {QStringLiteral("CLASSIC"), QStringLiteral("Summoner's Rift")},
        {QStringLiteral("ARAM"), QStringLiteral("ARAM")},
        {QStringLiteral("CHERRY"), QStringLiteral("Arena")},
        {QStringLiteral("URF"), QStringLiteral("URF")},
        {QStringLiteral("ONEFORALL"), QStringLiteral("Um Por Todos")},
        {QStringLiteral("NEXUSBLITZ"), QStringLiteral("Nexus Blitz")},
        {QStringLiteral("ULTBOOK"), QStringLiteral("Livro de Feitiços Supremo")},
        {QStringLiteral("PRACTICETOOL"), QStringLiteral("Modo Treino")},
        {QStringLiteral("TUTORIAL"), QStringLiteral("Tutorial")},
        {QStringLiteral("SWIFTPLAY"), QStringLiteral("Partida Rápida")},
    };
    return names.value(mode.toUpper(), mode);
}

QString LeagueEvents::classify(const QJsonObject &e, const QString &me, const EventOptions &options)
{
    auto isMe = [&](const QString &n) { return sameName(n, me); };
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

QString LeagueEvents::timelineLabel(const QJsonObject &e, const QString &me, const QString &team, const QStringList &allies)
{
    auto str = [&](const char *key) { return e.value(QLatin1String(key)).toString(); };
    auto isAlly = [&](const QString &n) {
        if (sameName(n, me))
            return true;
        for (const QString &a : allies)
            if (sameName(n, a))
                return true;
        return false;
    };
    const QString name = str("EventName");
    if (name == QLatin1String("ChampionKill") || name == QLatin1String("Multikill") || name == QLatin1String("Ace"))
        return classify(e, me, EventOptions{true, true});
    // Torres e inibidores: "Turret_T2_..." / "Barracks_T2_..." são do time CHAOS (T1 = ORDER).
    auto enemyStructure = [&](const QString &structure) {
        const bool order = structure.contains(QLatin1String("_T1_")), chaos = structure.contains(QLatin1String("_T2_"));
        if (team.isEmpty() || (!order && !chaos))
            return isAlly(str("KillerName"));
        return (order ? QStringLiteral("ORDER") : QStringLiteral("CHAOS")).compare(team, Qt::CaseInsensitive) != 0;
    };
    if (name == QLatin1String("TurretKilled"))
        return enemyStructure(str("TurretKilled")) ? QStringLiteral("Torre") : QString();
    if (name == QLatin1String("InhibKilled"))
        return enemyStructure(str("InhibKilled")) ? QStringLiteral("Inibidor") : QString();
    static const QList<QPair<const char *, const char *>> monsters = {
        {"DragonKill", "Dragão"}, {"BaronKill", "Barão"}, {"HeraldKill", "Arauto"},
        {"HordeKill", "Vastilarvas"}, {"AtakhanKill", "Atakhan"},
    };
    for (const auto &[event, label] : monsters)
        if (name == QLatin1String(event) && isAlly(str("KillerName")))
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
