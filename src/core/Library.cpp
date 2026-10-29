#include "Library.h"
#include "Log.h"
#include "Paths.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimeZone>

#include <mutex>

namespace aeonis {

namespace {

std::mutex gate;

void append(const LibEntry &e)
{
    std::lock_guard lock(gate);
    QFile f(Paths::libraryFile());
    if (!f.open(QIODevice::Append)) {
        Log::error(QStringLiteral("Falha ao gravar histórico"));
        return;
    }
    f.write(e.toJsonLine() + '\n');
}

void put(QJsonObject &o, const char *key, const QString &value)
{
    if (!value.isEmpty())
        o.insert(QLatin1String(key), value);
}

} // namespace

QByteArray LibEntry::toJsonLine() const
{
    QJsonObject o;
    o.insert(QStringLiteral("T"), t);
    o.insert(QStringLiteral("S"), s);
    o.insert(QStringLiteral("At"), Library::formatDate(at));
    put(o, "Game", game);
    put(o, "Exe", exe);
    put(o, "Path", path);
    put(o, "Kind", kind);
    put(o, "Label", label);
    if (duration)
        o.insert(QStringLiteral("Duration"), *duration);
    if (start.isValid())
        o.insert(QStringLiteral("Start"), Library::formatDate(start));
    if (!info.isEmpty())
        o.insert(QStringLiteral("Info"), info);
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

std::optional<LibEntry> LibEntry::fromJsonLine(const QByteArray &line)
{
    const QJsonDocument doc = QJsonDocument::fromJson(line);
    if (!doc.isObject())
        return std::nullopt;
    const QJsonObject o = doc.object();
    LibEntry e;
    e.t = o.value(QLatin1String("T")).toString();
    e.s = o.value(QLatin1String("S")).toString();
    e.at = Library::parseDate(o.value(QLatin1String("At")).toString());
    e.game = o.value(QLatin1String("Game")).toString();
    e.exe = o.value(QLatin1String("Exe")).toString();
    e.path = o.value(QLatin1String("Path")).toString();
    e.kind = o.value(QLatin1String("Kind")).toString();
    e.label = o.value(QLatin1String("Label")).toString();
    if (o.value(QLatin1String("Duration")).isDouble())
        e.duration = o.value(QLatin1String("Duration")).toDouble();
    if (o.value(QLatin1String("Start")).isString())
        e.start = Library::parseDate(o.value(QLatin1String("Start")).toString());
    e.info = o.value(QLatin1String("Info")).toObject();
    if (e.t.isEmpty())
        return std::nullopt;
    return e;
}

namespace Library {

QDateTime parseDate(const QString &input)
{
    // .NET: 2026-10-18T22:51:03.1234567-03:00 (ou sem fuso). O Qt aceita até milissegundos.
    static const QRegularExpression re(QStringLiteral(R"(^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2})(?:\.(\d+))?(Z|[+-]\d{2}:\d{2})?$)"));
    const auto m = re.match(input.trimmed());
    if (!m.hasMatch())
        return QDateTime::fromString(input, Qt::ISODateWithMs);
    QString ms = m.captured(2).left(3);
    while (ms.size() < 3) ms += QLatin1Char('0');
    QDateTime dt = QDateTime::fromString(m.captured(1) + QLatin1Char('.') + ms, QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz"));
    const QString zone = m.captured(3);
    if (zone == QLatin1String("Z")) {
        dt.setTimeZone(QTimeZone::UTC);
        dt = dt.toLocalTime();
    } else if (!zone.isEmpty()) {
        const int sign = zone[0] == QLatin1Char('-') ? -1 : 1;
        const int offset = sign * (zone.mid(1, 2).toInt() * 3600 + zone.mid(4, 2).toInt() * 60);
        dt.setTimeZone(QTimeZone::fromSecondsAheadOfUtc(offset));
        dt = dt.toLocalTime();
    }
    return dt;
}

QString formatDate(const QDateTime &dt)
{
    const QDateTime local = dt.toLocalTime();
    return local.toOffsetFromUtc(local.offsetFromUtc()).toString(Qt::ISODateWithMs);
}

void sessionStarted(const QString &session, const QString &game, const QString &exe)
{
    LibEntry e;
    e.t = QStringLiteral("session");
    e.s = session;
    e.at = QDateTime::currentDateTime();
    e.game = game;
    e.exe = exe;
    append(e);
}

void sessionEnded(const QString &session)
{
    LibEntry e;
    e.t = QStringLiteral("end");
    e.s = session;
    e.at = QDateTime::currentDateTime();
    append(e);
}

void event(const QString &session, const QString &label)
{
    LibEntry e;
    e.t = QStringLiteral("event");
    e.s = session;
    e.at = QDateTime::currentDateTime();
    e.label = label;
    append(e);
}

void matchInfo(const QString &session, const QJsonObject &info)
{
    LibEntry e;
    e.t = QStringLiteral("info");
    e.s = session;
    e.at = QDateTime::currentDateTime();
    e.info = info;
    append(e);
}

void clipSaved(const QString &session, const QString &path, const QString &kind, const QString &label,
               const QDateTime &requestedAt, double duration, const QDateTime &start)
{
    LibEntry e;
    e.t = QStringLiteral("clip");
    e.s = session;
    e.at = requestedAt;
    e.path = path;
    e.kind = kind;
    e.label = label;
    if (duration > 0)
        e.duration = duration;
    e.start = start.isValid() ? start : (duration > 0 ? requestedAt.addMSecs(qint64(-duration * 1000)) : QDateTime());
    append(e);
}

void derived(const QString &source, const QString &output, double offsetSeconds, double duration,
             const QString &kind, const QString &label)
{
    std::optional<LibEntry> src;
    for (const LibEntry &e : load())
        if (e.t == QLatin1String("clip") && e.path.compare(source, Qt::CaseInsensitive) == 0)
            src = e;
    LibEntry e;
    e.t = QStringLiteral("clip");
    e.s = src ? src->s : QString();
    e.at = QDateTime::currentDateTime();
    e.path = output;
    e.kind = !kind.isEmpty() ? kind
           : !src || src->kind == QLatin1String("full") || src->kind.isEmpty() ? QStringLiteral("manual") : src->kind;
    e.label = !label.isEmpty() ? label
            : (src && !src->label.isEmpty() ? src->label : QStringLiteral("Clipe")) + QStringLiteral(" (corte)");
    if (duration > 0)
        e.duration = duration;
    if (src && src->start.isValid())
        e.start = src->start.addMSecs(qint64(offsetSeconds * 1000));
    append(e);
}

QList<LibEntry> load()
{
    QList<LibEntry> list;
    std::lock_guard lock(gate);
    QFile f(Paths::libraryFile());
    if (!f.open(QIODevice::ReadOnly))
        return list;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (line.isEmpty())
            continue;
        if (auto e = LibEntry::fromJsonLine(line))
            list.append(*e);
    }
    return list;
}

} // namespace Library

} // namespace aeonis
