#include "Highlighter.h"
#include "core/Log.h"

#include <algorithm>
#include <cmath>

namespace aeonis {

Highlighter::Highlighter(Timing timing, Save save, QObject *parent)
    : QObject(parent), m_timing(std::move(timing)), m_save(std::move(save))
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &Highlighter::fire);
}

void Highlighter::trigger(const QString &reason)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!m_first.isValid())
        m_first = now;
    m_reasons << reason;
    m_deadline = now.addSecs(m_timing().second);
    const QDateTime max = m_first.addSecs(MaxExtendSeconds);
    if (m_deadline > max)
        m_deadline = max;
    m_timer.start(int(std::max<qint64>(0, now.msecsTo(m_deadline))));
    Log::info(QStringLiteral("Destaque: %1").arg(reason));
}

void Highlighter::flush()
{
    m_timer.stop();
    fire();
}

void Highlighter::fire()
{
    if (!m_first.isValid())
        return;
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime end = now < m_deadline ? now : m_deadline;
    const int seconds = m_timing().first + int(std::ceil(m_first.msecsTo(end) / 1000.0));
    const QString text = label(m_reasons);
    m_first = {};
    m_reasons.clear();
    m_save(std::max(seconds, 3), text);
}

QString Highlighter::label(const QStringList &r)
{
    static const QStringList priority = {
        QStringLiteral("Penta Kill"), QStringLiteral("Ace"), QStringLiteral("Quadra Kill"), QStringLiteral("Triple Kill"),
        QStringLiteral("Double Kill"), QStringLiteral("Barão"), QStringLiteral("Dragão"), QStringLiteral("Arauto"),
        QStringLiteral("Inibidor"), QStringLiteral("Torre"), QStringLiteral("Abate"), QStringLiteral("Assistência"),
        QStringLiteral("Morte"),
    };
    for (const QString &p : priority) {
        if (!r.contains(p))
            continue;
        const int kills = int(r.count(QStringLiteral("Abate")));
        return p == QLatin1String("Abate") && kills > 1 ? QStringLiteral("%1 Abates").arg(kills) : p;
    }
    return r.isEmpty() ? QStringLiteral("Destaque") : r.first();
}

QList<Highlighter::Span> Highlighter::spans(QList<std::pair<double, QString>> events, int pre, int post, double duration)
{
    std::sort(events.begin(), events.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QList<Span> out;
    double first = 0, last = 0;
    for (const auto &[t, reason] : events) {
        if (out.isEmpty() || t - last > pre + post || t - first > MaxExtendSeconds) {
            first = t;
            out.append({std::max(0.0, t - pre), 0, {}});
        }
        last = t;
        out.last().reasons << reason;
        out.last().end = duration > 0 ? std::min(duration, t + post) : t + post;
    }
    return out;
}

} // namespace aeonis
