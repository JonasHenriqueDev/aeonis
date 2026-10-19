#include "LibraryModel.h"
#include "capture/Clips.h"
#include "core/Library.h"
#include "core/MediaCache.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <optional>

namespace aeonis::LibraryModel {

namespace {

constexpr qint64 SessionGapMs = 45 * 60 * 1000;

struct Clip {
    QString id, path, name, kind, label, game;
    QDateTime created;
    std::optional<double> duration;
    qint64 size = 0;
    bool thumb = false;
    bool saved = false;
    QVariantList markers;
    QString session;
    double matchTime = -1; // início do clipe no relógio da partida (s); -1 = desconhecido
};

struct Match {
    QString id, game;
    QDateTime start, end;
    QList<Clip> clips;
    QVariantMap info;
    QVariantList events;
};

QVariantMap toVariant(const Clip &c)
{
    QVariantMap m;
    m.insert(QStringLiteral("id"), c.id);
    m.insert(QStringLiteral("path"), c.path);
    m.insert(QStringLiteral("url"), QUrl::fromLocalFile(c.path).toString());
    m.insert(QStringLiteral("name"), c.name);
    m.insert(QStringLiteral("kind"), c.kind);
    m.insert(QStringLiteral("label"), c.label);
    m.insert(QStringLiteral("game"), c.game);
    m.insert(QStringLiteral("created"), c.created);
    m.insert(QStringLiteral("duration"), c.duration ? *c.duration : -1.0);
    m.insert(QStringLiteral("size"), double(c.size));
    m.insert(QStringLiteral("thumb"), c.thumb ? QUrl::fromLocalFile(MediaCache::thumbPath(c.id)).toString() : QString());
    m.insert(QStringLiteral("saved"), c.saved);
    m.insert(QStringLiteral("markers"), c.markers);
    m.insert(QStringLiteral("matchTime"), c.matchTime);
    return m;
}

void order(QList<Clip> &clips)
{
    std::stable_sort(clips.begin(), clips.end(), [](const Clip &a, const Clip &b) {
        const int ka = a.kind == QLatin1String("full") ? 0 : 1, kb = b.kind == QLatin1String("full") ? 0 : 1;
        if (ka != kb)
            return ka < kb;
        return a.created < b.created;
    });
}

QDateTime clipStart(const Clip &c) { return c.created.addMSecs(-qint64(c.duration.value_or(0) * 1000)); }

Match synthetic(const QString &game, QList<Clip> clips)
{
    Match m;
    m.id = QStringLiteral("auto-") + clips.first().id;
    m.game = game;
    m.start = clipStart(clips.first());
    m.end = clips.first().created;
    for (const Clip &c : clips) {
        m.start = std::min(m.start, clipStart(c));
        m.end = std::max(m.end, c.created);
    }
    order(clips);
    m.clips = std::move(clips);
    return m;
}

} // namespace

QString labelFromName(const QString &name)
{
    static const QRegularExpression re(QStringLiteral(R"(_\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2}_(.+?)(?:_\d+)?$)"));
    const auto m = re.match(name);
    return m.hasMatch() ? m.captured(1) : name;
}

QString kindFromLabel(const QString &label)
{
    if (label == QLatin1String("Partida") || label == QLatin1String("Gravação") || label == QLatin1String("Recuperado"))
        return QStringLiteral("full");
    if (label == QLatin1String("Clipe") || label == QLatin1String("Teste") || label.endsWith(QLatin1String("(corte)")))
        return QStringLiteral("manual");
    return QStringLiteral("highlight");
}

Result build(const QString &outputDir)
{
    const QList<LibEntry> entries = Library::load();
    QHash<QString, LibEntry> sessions;
    QHash<QString, QDateTime> ends;
    QHash<QString, LibEntry> clipMeta; // caminho em minúsculas
    QHash<QString, QList<LibEntry>> events;
    QHash<QString, QJsonObject> infos;
    QHash<QString, QDateTime> fullStarts; // início da partida completa de cada sessão
    for (const LibEntry &e : entries) {
        if (e.t == QLatin1String("session")) sessions.insert(e.s, e);
        else if (e.t == QLatin1String("end")) ends.insert(e.s, e.at);
        else if (e.t == QLatin1String("clip") && !e.path.isEmpty()) {
            clipMeta.insert(QDir::toNativeSeparators(e.path).toLower(), e);
            if (e.kind == QLatin1String("full") && e.start.isValid() && !e.s.isEmpty()
                && (!fullStarts.contains(e.s) || e.start < fullStarts.value(e.s)))
                fullStarts.insert(e.s, e.start);
        }
        else if (e.t == QLatin1String("event")) events[e.s].append(e);
        else if (e.t == QLatin1String("info")) {
            QJsonObject &merged = infos[e.s];
            for (auto it = e.info.begin(); it != e.info.end(); ++it)
                merged.insert(it.key(), it.value());
        }
    }
    // Relógio da partida: o 0:00 do jogo (LoL) ou o início da partida completa.
    auto matchZero = [&](const QString &session) {
        const QDateTime game = QDateTime::fromString(infos.value(session).value(QLatin1String("gameStart")).toString(), Qt::ISODateWithMs);
        return game.isValid() ? game : fullStarts.value(session);
    };

    QList<Clip> clips;
    qint64 total = 0, unsaved = 0;
    Result result;
    if (QFileInfo(outputDir).isDir()) {
        QDirIterator it(outputDir, {QStringLiteral("*.mp4")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo f = it.nextFileInfo();
            const QString path = QDir::toNativeSeparators(f.absoluteFilePath());
            Clip c;
            c.path = path;
            c.size = f.size();
            total += c.size;
            c.saved = Clips::isSaved(path);
            if (!c.saved)
                unsaved += c.size;
            const auto metaIt = clipMeta.constFind(path.toLower());
            const LibEntry *meta = metaIt != clipMeta.constEnd() ? &*metaIt : nullptr;
            c.session = meta && !meta->s.isEmpty() ? meta->s : QString();
            const auto sessionIt = c.session.isEmpty() ? sessions.constEnd() : sessions.constFind(c.session);
            c.game = sessionIt != sessions.constEnd() && !sessionIt->game.isEmpty() ? sessionIt->game : f.dir().dirName();
            if (c.game.isEmpty())
                c.game = QStringLiteral("Outros");
            c.name = f.completeBaseName();
            c.label = meta && !meta->label.isEmpty() ? meta->label : labelFromName(c.name);
            c.kind = meta && !meta->kind.isEmpty() ? meta->kind : kindFromLabel(c.label);
            c.id = MediaCache::key(path, c.size, MediaCache::lastWriteTicks(path));
            c.duration = meta && meta->duration ? meta->duration : MediaCache::duration(c.id);
            c.thumb = MediaCache::hasThumb(c.id);
            c.created = meta && meta->at.isValid() ? meta->at : f.lastModified();
            if (!c.session.isEmpty() && meta && meta->start.isValid() && c.duration) {
                for (const LibEntry &ev : events.value(c.session)) {
                    const double t = meta->start.msecsTo(ev.at) / 1000.0;
                    if (t >= 0 && t <= *c.duration) {
                        QVariantMap mk;
                        mk.insert(QStringLiteral("t"), std::round(t * 10) / 10);
                        mk.insert(QStringLiteral("label"), ev.label);
                        c.markers.append(mk);
                    }
                }
            }
            if (!c.session.isEmpty() && meta && meta->start.isValid() && c.kind != QLatin1String("full")) {
                const QDateTime zero = matchZero(c.session);
                if (zero.isValid())
                    c.matchTime = std::max(0.0, std::round(zero.msecsTo(meta->start) / 100.0) / 10);
            }
            if (!c.thumb || !c.duration) {
                QVariantMap missing;
                missing.insert(QStringLiteral("id"), c.id);
                missing.insert(QStringLiteral("path"), path);
                result.missingThumbs.append(missing);
            }
            clips.append(c);
        }
    }

    QList<Match> matches;
    QHash<QString, QList<Clip>> bySession;
    QHash<QString, QList<Clip>> byGame;
    QStringList sessionOrder, gameOrder;
    for (const Clip &c : clips) {
        if (!c.session.isEmpty()) {
            if (!bySession.contains(c.session)) sessionOrder << c.session;
            bySession[c.session].append(c);
        } else {
            if (!byGame.contains(c.game)) gameOrder << c.game;
            byGame[c.game].append(c);
        }
    }
    for (const QString &s : sessionOrder) {
        QList<Clip> list = bySession.value(s);
        Match m;
        m.id = s;
        const auto se = sessions.constFind(s);
        m.game = se != sessions.constEnd() && !se->game.isEmpty() ? se->game : list.first().game;
        if (se != sessions.constEnd() && se->at.isValid()) {
            m.start = se->at;
        } else {
            m.start = clipStart(list.first());
            for (const Clip &c : list) m.start = std::min(m.start, clipStart(c));
        }
        if (ends.contains(s)) {
            m.end = ends.value(s);
        } else {
            m.end = list.first().created;
            for (const Clip &c : list) m.end = std::max(m.end, c.created);
        }
        order(list);
        m.clips = list;
        const QJsonObject info = infos.value(s);
        if (!info.isEmpty()) {
            m.info = info.toVariantMap();
            const QString icon = MediaCache::championIconPath(info.value(QLatin1String("championId")).toString());
            if (info.contains(QLatin1String("championId")) && QFileInfo::exists(icon))
                m.info.insert(QStringLiteral("championIcon"), QUrl::fromLocalFile(icon).toString());
        }
        const QDateTime zero = matchZero(s);
        for (const LibEntry &ev : events.value(s)) {
            QVariantMap mv;
            mv.insert(QStringLiteral("label"), ev.label);
            mv.insert(QStringLiteral("t"), zero.isValid() ? std::round(zero.msecsTo(ev.at) / 100.0) / 10 : -1.0);
            m.events.append(mv);
        }
        matches.append(m);
    }
    // Sem histórico: agrupa por jogo, abrindo uma nova "partida" quando há um intervalo grande entre vídeos.
    for (const QString &g : gameOrder) {
        QList<Clip> list = byGame.value(g);
        std::sort(list.begin(), list.end(), [](const Clip &a, const Clip &b) { return a.created < b.created; });
        QList<Clip> current;
        for (const Clip &c : list) {
            if (!current.isEmpty() && current.last().created.msecsTo(c.created) > SessionGapMs) {
                matches.append(synthetic(g, current));
                current.clear();
            }
            current.append(c);
        }
        if (!current.isEmpty())
            matches.append(synthetic(g, current));
    }
    std::sort(matches.begin(), matches.end(), [](const Match &a, const Match &b) { return a.start > b.start; });

    QVariantList matchList;
    QStringList games;
    QHash<QString, int> gameClips;
    for (const Match &m : matches) {
        if (!games.contains(m.game)) games << m.game; // em ordem da partida mais recente
        gameClips[m.game] += int(m.clips.size());
        QVariantMap mv;
        mv.insert(QStringLiteral("id"), m.id);
        mv.insert(QStringLiteral("game"), m.game);
        mv.insert(QStringLiteral("start"), m.start);
        mv.insert(QStringLiteral("end"), m.end);
        mv.insert(QStringLiteral("info"), m.info);
        mv.insert(QStringLiteral("events"), m.events);
        QVariantList cl;
        for (const Clip &c : m.clips)
            cl.append(toVariant(c));
        mv.insert(QStringLiteral("clips"), cl);
        matchList.append(mv);
    }
    QVariantList gameList;
    for (const QString &g : games) {
        QVariantMap gv;
        gv.insert(QStringLiteral("name"), g);
        const QString icon = MediaCache::gameIconPath(g);
        gv.insert(QStringLiteral("icon"), QFileInfo::exists(icon) ? QUrl::fromLocalFile(icon).toString() : QString());
        gv.insert(QStringLiteral("clips"), gameClips.value(g));
        gameList.append(gv);
    }
    result.data.insert(QStringLiteral("matches"), matchList);
    result.data.insert(QStringLiteral("games"), gameList);
    result.data.insert(QStringLiteral("totalBytes"), double(total));
    result.data.insert(QStringLiteral("unsavedBytes"), double(unsaved));
    result.data.insert(QStringLiteral("outputDir"), outputDir);
    return result;
}

} // namespace aeonis::LibraryModel
