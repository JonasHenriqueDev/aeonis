#include "Clips.h"
#include "core/Log.h"
#include "core/Paths.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QTimeZone>

#include <algorithm>
#include <mutex>
#include <optional>

namespace aeonis::Clips {

namespace {

std::mutex sync;
std::optional<QSet<QString>> saved; // caminhos completos em minúsculas

QString norm(const QString &path) { return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).toLower(); }

QSet<QString> &savedSet()
{
    if (saved)
        return *saved;
    saved.emplace();
    QFile f(Paths::savedFile());
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
        for (const QJsonValue &v : arr)
            if (QFileInfo::exists(v.toString()))
                saved->insert(norm(v.toString()));
    }
    return *saved;
}

void persist()
{
    QStringList list(saved->begin(), saved->end());
    list.sort();
    QSaveFile f(Paths::savedFile());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonArray::fromStringList(list)).toJson(QJsonDocument::Compact));
        f.commit();
    } else {
        Log::error(QStringLiteral("Falha ao gravar a lista de vídeos salvos"));
    }
}

QList<QFileInfo> videos(const QString &outputDir)
{
    QList<QFileInfo> files;
    QDirIterator it(outputDir, {QStringLiteral("*.mp4")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        files.append(it.nextFileInfo());
    return files;
}

} // namespace

QString sanitize(const QString &s)
{
    static const QString invalid = QStringLiteral("<>:\"/\\|?*");
    QString r;
    r.reserve(s.size());
    for (QChar c : s)
        r += (c.unicode() < 32 || invalid.contains(c)) ? QLatin1Char('_') : c;
    r = r.trimmed();
    while (r.endsWith(QLatin1Char('.')))
        r.chop(1);
    return r.isEmpty() ? QStringLiteral("Clip") : r;
}

QString newPath(const QString &outputDir, const QString &game, const QString &label)
{
    const QString dir = QDir::toNativeSeparators(outputDir + QLatin1Char('/') + sanitize(game));
    QDir().mkpath(dir);
    const QString base = QStringLiteral("%1_%2_%3").arg(sanitize(game),
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss")), sanitize(label));
    QString path = dir + QLatin1Char('\\') + base + QStringLiteral(".mp4");
    for (int i = 2; QFileInfo::exists(path) || QFileInfo::exists(path + QStringLiteral(".part")); ++i)
        path = dir + QLatin1Char('\\') + QStringLiteral("%1_%2.mp4").arg(base).arg(i);
    return path;
}

bool isSaved(const QString &path)
{
    std::lock_guard lock(sync);
    return savedSet().contains(norm(path));
}

void setSaved(const QStringList &paths, bool value)
{
    std::lock_guard lock(sync);
    QSet<QString> &set = savedSet();
    for (const QString &p : paths) {
        if (value) set.insert(norm(p));
        else set.remove(norm(p));
    }
    persist();
}

int enforceQuota(const QString &outputDir, double maxGb)
{
    if (maxGb <= 0 || !QFileInfo(outputDir).isDir())
        return 0;
    std::lock_guard lock(sync);
    const qint64 limit = qint64(maxGb * 1024.0 * 1024.0 * 1024.0);
    const QSet<QString> &set = savedSet();
    QList<QFileInfo> files;
    for (const QFileInfo &f : videos(outputDir))
        if (!set.contains(norm(f.absoluteFilePath())))
            files.append(f);
    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified(QTimeZone::UTC) < b.lastModified(QTimeZone::UTC);
    });
    qint64 total = 0;
    for (const QFileInfo &f : files)
        total += f.size();
    int removed = 0;
    // Nunca apaga o vídeo mais recente (o que acabou de ser gravado), mesmo que sozinho passe do limite.
    for (int i = 0; i + 1 < files.size() && total > limit; ++i) {
        const qint64 size = files[i].size();
        if (QFile::remove(files[i].absoluteFilePath())) {
            total -= size;
            ++removed;
            Log::info(QStringLiteral("Limite de armazenamento: removido %1").arg(QDir::toNativeSeparators(files[i].absoluteFilePath())));
        } else {
            Log::warn(QStringLiteral("Limite de armazenamento: não foi possível remover %1").arg(files[i].absoluteFilePath()));
        }
    }
    return removed;
}

qint64 unsavedBytes(const QString &outputDir)
{
    if (!QFileInfo(outputDir).isDir())
        return 0;
    std::lock_guard lock(sync);
    const QSet<QString> &set = savedSet();
    qint64 total = 0;
    for (const QFileInfo &f : videos(outputDir))
        if (!set.contains(norm(f.absoluteFilePath())))
            total += f.size();
    return total;
}

qint64 removePartials(const QString &outputDir)
{
    if (!QFileInfo(outputDir).isDir())
        return 0;
    qint64 freed = 0;
    QDirIterator it(outputDir, {QStringLiteral("*.mp4.part")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo f = it.nextFileInfo();
        if (QFile::remove(f.absoluteFilePath())) {
            freed += f.size();
            Log::info(QStringLiteral("Removido vídeo incompleto: %1").arg(QDir::toNativeSeparators(f.absoluteFilePath())));
        }
    }
    return freed;
}

} // namespace aeonis::Clips
