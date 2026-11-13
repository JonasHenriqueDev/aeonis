#include "DeadlockMatch.h"
#include "core/Log.h"
#include "core/MediaCache.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <algorithm>

namespace aeonis {

namespace DeadlockMatch {

namespace {
constexpr double MultikillGapSeconds = 12; // abates com até esse intervalo contam como multikill
const char *const ApiBase = "https://api.deadlock-api.com";

QString multikillName(int n)
{
    switch (n) {
    case 2: return QStringLiteral("Double Kill");
    case 3: return QStringLiteral("Triple Kill");
    case 4: return QStringLiteral("Quadra Kill");
    case 5: return QStringLiteral("Penta Kill");
    default: return n > 5 ? QStringLiteral("%1 Kills").arg(n) : QString();
    }
}

QString modeName(int matchMode)
{
    switch (matchMode) {
    case 1: return QStringLiteral("Normal");
    case 2: return QStringLiteral("Lobby privado");
    case 3: return QStringLiteral("Contra bots");
    case 4: return QStringLiteral("Ranqueada");
    default: return {};
    }
}
} // namespace

qint64 accountId(quint64 steamId64)
{
    constexpr quint64 Base = 76561197960265728ULL;
    return steamId64 > Base ? qint64(steamId64 - Base) : qint64(steamId64);
}

QString objectiveName(int id)
{
    // ECitadelTeamObjective: 1-4 guardiões, 5-8 walkers, 9 patrono, 10-11 santuários, 12-15 guardiões da base
    // (0 é o núcleo: destruído = fim da partida, não marca).
    if (id >= 1 && id <= 4)
        return QStringLiteral("Guardião");
    if (id >= 5 && id <= 8)
        return QStringLiteral("Walker");
    if (id == 9)
        return QStringLiteral("Patrono");
    if (id == 10 || id == 11)
        return QStringLiteral("Santuário");
    if (id >= 12 && id <= 15)
        return QStringLiteral("Guardião da base");
    return {};
}

Parsed parse(const QJsonObject &root, qint64 account, const EventOptions &options)
{
    Parsed out;
    const QJsonObject m = root.value(QLatin1String("match_info")).toObject();
    const QJsonArray players = m.value(QLatin1String("players")).toArray();
    QJsonObject me;
    for (const QJsonValue &v : players)
        if (v.toObject().value(QLatin1String("account_id")).toInteger() == account)
            me = v.toObject();
    const int slot = me.value(QLatin1String("player_slot")).toInt(-1);
    const int team = me.value(QLatin1String("team")).toInt(-1);
    if (me.isEmpty() || slot < 0)
        return out;
    out.ok = true;
    out.heroId = me.value(QLatin1String("hero_id")).toInt();

    QJsonObject &info = out.info;
    info.insert(QStringLiteral("kills"), me.value(QLatin1String("kills")).toInt());
    info.insert(QStringLiteral("deaths"), me.value(QLatin1String("deaths")).toInt());
    info.insert(QStringLiteral("assists"), me.value(QLatin1String("assists")).toInt());
    info.insert(QStringLiteral("level"), me.value(QLatin1String("level")).toInt());
    info.insert(QStringLiteral("cs"), me.value(QLatin1String("last_hits")).toInt());
    if (team >= 0 && m.contains(QLatin1String("winning_team")))
        info.insert(QStringLiteral("result"), m.value(QLatin1String("winning_team")).toInt(-2) == team ? QStringLiteral("Win") : QStringLiteral("Lose"));
    if (const QString mode = modeName(m.value(QLatin1String("match_mode")).toInt()); !mode.isEmpty())
        info.insert(QStringLiteral("mode"), mode);
    info.insert(QStringLiteral("matchId"), double(m.value(QLatin1String("match_id")).toInteger()));

    // Abates: as mortes dos outros jogadores em que o matador foi este jogador.
    QList<double> kills;
    for (const QJsonValue &v : players) {
        const QJsonObject p = v.toObject();
        for (const QJsonValue &d : p.value(QLatin1String("death_details")).toArray()) {
            const QJsonObject death = d.toObject();
            const double t = death.value(QLatin1String("game_time_s")).toDouble(-1);
            if (t < 0)
                continue;
            if (p.value(QLatin1String("player_slot")).toInt(-1) == slot)
                out.events.append({t, QStringLiteral("Morte"), options.deaths});
            else if (death.value(QLatin1String("killer_player_slot")).toInt(-1) == slot)
                kills.append(t);
        }
    }
    std::sort(kills.begin(), kills.end());
    int streak = 0;
    for (int i = 0; i < kills.size(); ++i) {
        streak = i > 0 && kills[i] - kills[i - 1] <= MultikillGapSeconds ? streak + 1 : 1;
        out.events.append({kills[i], QStringLiteral("Abate"), true});
        if (streak >= 2)
            out.events.append({kills[i], multikillName(streak), true});
    }
    // Objetivos inimigos destruídos (`team` é o dono da estrutura).
    for (const QJsonValue &v : m.value(QLatin1String("objectives")).toArray()) {
        const QJsonObject o = v.toObject();
        const double t = o.value(QLatin1String("destroyed_time_s")).toDouble();
        const QString name = objectiveName(o.value(QLatin1String("team_objective_id")).toInt(-1));
        if (t > 0 && !name.isEmpty() && o.value(QLatin1String("team")).toInt(team) != team)
            out.events.append({t, name, false});
    }
    for (const QJsonValue &v : m.value(QLatin1String("mid_boss")).toArray()) {
        const QJsonObject b = v.toObject();
        const double t = b.value(QLatin1String("destroyed_time_s")).toDouble();
        if (t > 0 && b.value(QLatin1String("team_claimed")).toInt(-1) == team)
            out.events.append({t, QStringLiteral("Chefe do meio"), false});
    }
    std::stable_sort(out.events.begin(), out.events.end(), [](const Event &a, const Event &b) { return a.gameTime < b.gameTime; });

    for (const QJsonValue &v : m.value(QLatin1String("match_pauses")).toArray()) {
        const QJsonObject p = v.toObject();
        const double length = p.value(QLatin1String("pause_duration_s")).toDouble();
        if (length > 0)
            out.pauses.append({p.value(QLatin1String("game_time_s")).toDouble(), length});
    }
    return out;
}

QDateTime wallTime(const QDateTime &clockZero, double gameTime, const QList<std::pair<double, double>> &pauses)
{
    double paused = 0;
    for (const auto &[at, length] : pauses)
        if (at < gameTime)
            paused += length;
    return clockZero.addMSecs(qint64((gameTime + paused) * 1000));
}

} // namespace DeadlockMatch

// ---------------------------------------------------------------- consulta

DeadlockMatchFetcher::DeadlockMatchFetcher(EventSource::Options options, QObject *parent)
    : QObject(parent), m_options(std::move(options)), m_net(new QNetworkAccessManager(this))
{
}

void DeadlockMatchFetcher::add(const Job &job)
{
    if (job.matchId <= 0 || job.accountId <= 0 || !job.clockZero.isValid() || job.session.isEmpty()) {
        Log::warn(QStringLiteral("Deadlock: sem dados para buscar os eventos da partida (id %1, conta %2, relógio %3).")
                      .arg(job.matchId).arg(job.accountId > 0 ? QStringLiteral("ok") : QStringLiteral("?"))
                      .arg(job.clockZero.isValid() ? QStringLiteral("ok") : QStringLiteral("?")));
        return;
    }
    if (m_seen.contains(job.matchId)) // fim da partida e jogo fechado podem pedir a mesma
        return;
    m_seen.insert(job.matchId);
    Log::info(QStringLiteral("Deadlock: buscando os eventos da partida %1 na deadlock-api.com.").arg(job.matchId));
    schedule(job, FirstDelaySeconds);
}

void DeadlockMatchFetcher::schedule(const Job &job, int seconds)
{
    QTimer::singleShot(seconds * 1000, this, [this, job] { fetch(job); });
}

void DeadlockMatchFetcher::fetch(Job job)
{
    ++job.attempts;
    QNetworkRequest req(QUrl(QStringLiteral("%1/v1/matches/%2/metadata").arg(QLatin1String(DeadlockMatch::ApiBase)).arg(job.matchId)));
    req.setTransferTimeout(30000);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Aeonis"));
    QNetworkReply *r = m_net->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, job] {
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject root = status == 200 ? QJsonDocument::fromJson(r->readAll()).object() : QJsonObject();
        if (!root.contains(QLatin1String("match_info"))) {
            // Ainda não publicada (404), limite (429), servidor (5xx) ou rede (0): tenta de novo; o resto é definitivo.
            const bool transient = status == 0 || status == 404 || status == 429 || status >= 500 || status == 200;
            if (transient && job.attempts < MaxAttempts)
                schedule(job, RetrySeconds);
            else
                Log::warn(QStringLiteral("Deadlock: a partida %1 não apareceu na deadlock-api.com (último status %2).")
                              .arg(job.matchId).arg(status));
            return;
        }
        const DeadlockMatch::Parsed parsed = DeadlockMatch::parse(root, job.accountId, m_options());
        if (!parsed.ok) {
            Log::warn(QStringLiteral("Deadlock: jogador não encontrado na partida %1.").arg(job.matchId));
            return;
        }
        finish(job, parsed);
    });
}

void DeadlockMatchFetcher::finish(const Job &job, const DeadlockMatch::Parsed &parsed)
{
    auto deliver = [this, job, parsed](const QString &hero) {
        Result res;
        res.job = job;
        res.info = parsed.info;
        if (!hero.isEmpty()) {
            res.info.insert(QStringLiteral("champion"), hero);
            res.info.insert(QStringLiteral("championId"), QStringLiteral("deadlock-%1").arg(parsed.heroId));
        }
        for (const DeadlockMatch::Event &e : parsed.events)
            res.events.append({DeadlockMatch::wallTime(job.clockZero, e.gameTime, parsed.pauses), e});
        Log::info(QStringLiteral("Deadlock: %1 eventos da partida %2.").arg(res.events.size()).arg(job.matchId));
        emit ready(res);
    };
    if (parsed.heroId <= 0) {
        deliver({});
        return;
    }
    // Nome (em português) e ícone do herói; sem eles a partida sai só com o KDA.
    QNetworkRequest req(QUrl(QStringLiteral("%1/v1/assets/heroes/%2?language=brazilian").arg(QLatin1String(DeadlockMatch::ApiBase)).arg(parsed.heroId)));
    req.setTransferTimeout(15000);
    QNetworkReply *r = m_net->get(req);
    const QString iconPath = MediaCache::championIconPath(QStringLiteral("deadlock-%1").arg(parsed.heroId));
    connect(r, &QNetworkReply::finished, this, [this, r, deliver, iconPath] {
        r->deleteLater();
        const QJsonObject hero = r->error() == QNetworkReply::NoError ? QJsonDocument::fromJson(r->readAll()).object() : QJsonObject();
        const QString name = hero.value(QLatin1String("name")).toString();
        const QUrl icon(hero.value(QLatin1String("images")).toObject().value(QLatin1String("icon_image_small")).toString());
        // Só da CDN da própria API (o endereço vem na resposta) e com tamanho de ícone.
        const bool trusted = icon.scheme() == QLatin1String("https")
            && (icon.host() == QLatin1String("deadlock-api.com") || icon.host().endsWith(QLatin1String(".deadlock-api.com")));
        if (QFileInfo::exists(iconPath) || !trusted) {
            deliver(name);
            return;
        }
        QNetworkRequest img(icon);
        img.setTransferTimeout(15000);
        QNetworkReply *ri = m_net->get(img);
        connect(ri, &QNetworkReply::finished, this, [ri, deliver, name, iconPath] {
            ri->deleteLater();
            const QByteArray png = ri->read(2 << 20);
            if (ri->error() == QNetworkReply::NoError && png.startsWith("\x89PNG") && ri->atEnd()) {
                QSaveFile f(iconPath); // atômico: nunca fica um ícone pela metade
                if (f.open(QIODevice::WriteOnly) && f.write(png) == png.size())
                    f.commit();
            }
            deliver(name);
        });
    });
}

} // namespace aeonis
