#include "Paths.h"

#include <QDir>
#include <QFileInfo>

#include <windows.h>
#include <shlobj.h>

namespace aeonis::Paths {

namespace {

QString knownFolder(REFKNOWNFOLDERID id)
{
    PWSTR path = nullptr;
    QString result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path)))
        result = QString::fromWCharArray(path);
    CoTaskMemFree(path);
    return result;
}

QString join(const QString &a, const QString &b) { return QDir::toNativeSeparators(a + QLatin1Char('/') + b); }

} // namespace

QString appData()
{
    static const QString dir = [] {
        const QString custom = qEnvironmentVariable("AEONIS_DATA");
        if (!custom.isEmpty())
            return QDir::toNativeSeparators(QFileInfo(custom).absoluteFilePath());
        return join(knownFolder(FOLDERID_LocalAppData), QStringLiteral("Aeonis"));
    }();
    return dir;
}

QString settingsFile() { return join(appData(), QStringLiteral("settings.json")); }
QString savedFile() { return join(appData(), QStringLiteral("saved.json")); }
QString logFile() { return join(appData(), QStringLiteral("aeonis.log")); }
QString buffer() { return join(appData(), QStringLiteral("buffer")); }
QString thumbs() { return join(appData(), QStringLiteral("thumbs")); }
QString icons() { return join(appData(), QStringLiteral("icons")); }
QString libraryFile() { return join(appData(), QStringLiteral("library.jsonl")); }

QString defaultVideos()
{
    QString videos = knownFolder(FOLDERID_Videos);
    if (videos.isEmpty())
        videos = QDir::homePath() + QStringLiteral("/Videos");
    return join(videos, QStringLiteral("Aeonis"));
}

QString appDir()
{
    wchar_t path[MAX_PATH * 2]{};
    GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
    return QDir::toNativeSeparators(QFileInfo(QString::fromWCharArray(path)).absolutePath());
}

void ensure()
{
    for (const QString &dir : {appData(), buffer(), thumbs(), icons()})
        QDir().mkpath(dir);
}

} // namespace aeonis::Paths
