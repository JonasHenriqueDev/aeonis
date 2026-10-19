#include "MediaCache.h"
#include "Log.h"
#include "Media.h"
#include "Paths.h"
#include "capture/Clips.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

namespace aeonis::MediaCache {

QString key(const QString &file, qint64 size, qint64 ticks)
{
    const QString text = QStringLiteral("%1|%2|%3").arg(QDir::toNativeSeparators(file)).arg(size).arg(ticks);
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha1).toHex().left(20));
}

qint64 lastWriteTicks(const QString &file)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(file).utf16()), GetFileExInfoStandard, &data))
        return 0;
    const qint64 ft = (qint64(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
    return ft + 504'911'232'000'000'000LL; // 1601-01-01 -> 0001-01-01
}

QString key(const QString &file)
{
    return key(file, QFileInfo(file).size(), lastWriteTicks(file));
}

QString thumbPath(const QString &k) { return QDir::toNativeSeparators(Paths::thumbs() + QLatin1Char('/') + k + QStringLiteral(".jpg")); }

static QString durPath(const QString &k) { return Paths::thumbs() + QLatin1Char('/') + k + QStringLiteral(".dur"); }

bool hasThumb(const QString &k) { return QFileInfo::exists(thumbPath(k)); }

std::optional<double> duration(const QString &k)
{
    QFile f(durPath(k));
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    bool ok = false;
    const double d = QString::fromLatin1(f.readAll()).trimmed().toDouble(&ok);
    return ok && d > 0 ? std::optional<double>(d) : std::nullopt;
}

std::optional<double> generate(const QString &video, const QString &k)
{
    const double d = Media::thumbnail(video, thumbPath(k));
    if (d <= 0)
        return std::nullopt;
    QFile f(durPath(k));
    if (f.open(QIODevice::WriteOnly))
        f.write(QByteArray::number(d, 'g', 10));
    return d;
}

QString gameIconPath(const QString &game)
{
    return QDir::toNativeSeparators(Paths::icons() + QLatin1Char('/') + Clips::sanitize(game) + QStringLiteral(".png"));
}

QString championIconPath(const QString &championId)
{
    return QDir::toNativeSeparators(Paths::icons() + QStringLiteral("/lol-") + Clips::sanitize(championId) + QStringLiteral(".png"));
}

void saveGameIcon(const QString &game, const QString &exe)
{
    if (exe.isEmpty() || !QFileInfo::exists(exe))
        return;
    const QString png = gameIconPath(game);
    if (QFileInfo::exists(png))
        return;
    HICON large = nullptr;
    const std::wstring path = QDir::toNativeSeparators(exe).toStdWString();
    // Ícone grande (até 256 px) do executável; cai para o de 32 px.
    if (SHDefExtractIconW(path.c_str(), 0, 0, &large, nullptr, MAKELONG(64, 32)) != S_OK || !large)
        ExtractIconExW(path.c_str(), 0, &large, nullptr, 1);
    if (!large) {
        Log::warn(QStringLiteral("Ícone de %1: não encontrado em %2").arg(game, exe));
        return;
    }
    const QImage img = QImage::fromHICON(large);
    DestroyIcon(large);
    if (!img.isNull())
        img.save(png, "PNG");
}

} // namespace aeonis::MediaCache
