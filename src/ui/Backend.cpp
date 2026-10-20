#include "Backend.h"
#include "LibraryModel.h"
#include "SettingsSchema.h"
#include "app/App.h"
#include "app/AutoUpdater.h"
#include "capture/Clips.h"
#include "core/Async.h"
#include "core/Library.h"
#include "core/Log.h"
#include "core/Media.h"
#include "core/MediaCache.h"
#include "games/GameCatalog.h"
#include "share/YouTube.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QClipboard>
#include <QProcess>
#include <QRegularExpression>
#include <QThreadPool>
#include <QUrl>

namespace aeonis {

namespace {

QThreadPool &thumbPool()
{
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(1); // miniaturas uma por vez, em prioridade baixa
        p->setThreadPriority(QThread::LowPriority);
        return p;
    }();
    return *pool;
}

} // namespace

Backend::Backend(App *app, QObject *parent) : QObject(parent), m_app(app)
{
    connect(app, &App::statusChanged, this, &Backend::statusChanged);
    connect(app, &App::libraryChanged, this, [this] {
        if (m_active)
            refresh();
    });
    connect(app, &App::toast, this, &Backend::toast);
}

QVariantMap Backend::status() const { return m_app->status(); }

QVariantList Backend::settingsFields() const { return SettingsSchema::fields(m_app->settings()); }

QObject *Backend::updater() const { return m_app->updater(); }

QObject *Backend::youtube() const { return m_app->youtube(); }

void Backend::setActive(bool active)
{
    m_active = active;
    if (active) {
        refresh();
        emit settingsChanged();
    } else {
        ++m_thumbGeneration;
        m_library.clear();
    }
}

void Backend::refresh()
{
    if (m_loading) {
        m_reloadPending = true;
        return;
    }
    m_loading = true;
    emit loadingChanged();
    const QString dir = m_app->settings().outputDir;
    runAsync(this, [dir] { return LibraryModel::build(dir); }, [this](LibraryModel::Result r) {
        m_loading = false;
        emit loadingChanged();
        if (!m_active)
            return;
        m_library = r.data;
        emit libraryChanged();
        generateThumbs(r.missingThumbs);
        if (m_reloadPending) {
            m_reloadPending = false;
            refresh();
        }
    });
}

void Backend::generateThumbs(const QVariantList &missing)
{
    const int generation = ++m_thumbGeneration;
    for (const QVariant &v : missing) {
        const QVariantMap m = v.toMap();
        const QString id = m.value(QStringLiteral("id")).toString();
        const QString path = m.value(QStringLiteral("path")).toString();
        runAsync(this, [this, id, path, generation]() -> std::optional<double> {
            if (generation != m_thumbGeneration.load())
                return std::nullopt;
            return MediaCache::generate(path, id);
        }, [this, id, generation](std::optional<double> duration) {
            if (generation != m_thumbGeneration.load())
                return;
            const QString url = MediaCache::hasThumb(id) ? QUrl::fromLocalFile(MediaCache::thumbPath(id)).toString() : QString();
            emit thumbReady(id, url, duration.value_or(-1));
        }, &thumbPool());
    }
}

QString Backend::safeVideo(const QString &path) const
{
    if (path.isEmpty())
        return {};
    const QString full = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    QString root = QDir::toNativeSeparators(QFileInfo(m_app->settings().outputDir).absoluteFilePath());
    if (!root.endsWith(QLatin1Char('\\')))
        root += QLatin1Char('\\');
    if (!full.startsWith(root, Qt::CaseInsensitive) || !full.endsWith(QLatin1String(".mp4"), Qt::CaseInsensitive)
        || !QFileInfo::exists(full))
        return {};
    return full;
}

void Backend::reveal(const QString &path)
{
    if (const QString v = safeVideo(path); !v.isEmpty())
        QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), v});
}

void Backend::openExternal(const QString &path)
{
    if (const QString v = safeVideo(path); !v.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(v));
}

QVariantMap Backend::gameFromExe(const QUrl &url) const
{
    const QString path = url.toLocalFile();
    const QFileInfo info(path);
    if (!info.isFile() || info.suffix().compare(QLatin1String("exe"), Qt::CaseInsensitive) != 0)
        return {};
    const QString name = GameCatalog::displayName(path);
    MediaCache::saveGameIcon(name, path);
    QVariantMap m;
    m.insert(QStringLiteral("name"), name);
    m.insert(QStringLiteral("process"), info.completeBaseName());
    m.insert(QStringLiteral("icon"), gameIconUrl(name));
    return m;
}

QString Backend::gameIconUrl(const QString &game) const
{
    // "Counter-Strike 2" -> counter-strike-2
    QString slug = game.toLower();
    slug.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    slug.remove(QRegularExpression(QStringLiteral("^-+|-+$")));
    if (!slug.isEmpty() && QFile::exists(QStringLiteral(":/games/%1.png").arg(slug)))
        return QStringLiteral("qrc:/games/%1.png").arg(slug);
    const QString png = MediaCache::gameIconPath(game);
    return QFileInfo::exists(png) ? QUrl::fromLocalFile(png).toString() : QString();
}

void Backend::openFolder() { m_app->openOutputFolder(); }

void Backend::openLog() { m_app->openLog(); }

void Backend::deleteClips(const QStringList &paths)
{
    int n = 0;
    QStringList removed;
    for (const QString &p : paths) {
        const QString v = safeVideo(p);
        if (v.isEmpty())
            continue;
        if (QFile::moveToTrash(v)) {
            ++n;
            removed << v;
        } else {
            Log::warn(QStringLiteral("Excluir %1: falhou").arg(v));
        }
    }
    if (!removed.isEmpty())
        Clips::setSaved(removed, false);
    emit toast(n == 1 ? QStringLiteral("Vídeo movido para a lixeira.") : QStringLiteral("%1 vídeos movidos para a lixeira.").arg(n),
               n > 0 ? QStringLiteral("info") : QStringLiteral("error"));
    refresh();
}

void Backend::setSaved(const QStringList &paths, bool saved)
{
    QStringList valid;
    for (const QString &p : paths)
        if (const QString v = safeVideo(p); !v.isEmpty())
            valid << v;
    if (valid.isEmpty())
        return;
    Clips::setSaved(valid, saved);
    emit toast(saved ? QStringLiteral("Salvo: não será apagado pelo limite de armazenamento.")
                     : QStringLiteral("Removido dos salvos."), QStringLiteral("info"));
    refresh();
}

void Backend::trim(const QString &path, double start, double end)
{
    const QString source = safeVideo(path);
    if (source.isEmpty())
        return;
    if (end - start < 0.5) {
        emit toast(QStringLiteral("O corte precisa ter pelo menos meio segundo."), QStringLiteral("error"));
        emit trimDone(false, {});
        return;
    }
    const QFileInfo fi(source);
    QString output = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral("_corte.mp4");
    for (int i = 2; QFileInfo::exists(output); ++i)
        output = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName() + QStringLiteral("_corte%1.mp4").arg(i);
    output = QDir::toNativeSeparators(output);
    runAsync(this, [source, output, start, end] {
        const Media::RemuxResult r = Media::trim(source, output, start, end);
        if (r.ok)
            Library::derived(source, output, r.startOffset, r.duration);
        else
            Log::error(QStringLiteral("Falha ao cortar %1: %2").arg(source, r.error));
        return r.ok;
    }, [this, output](bool ok) {
        emit trimDone(ok, ok ? output : QString());
        emit toast(ok ? QStringLiteral("Corte salvo.") : QStringLiteral("Falha ao cortar o vídeo (veja o log)."),
                   ok ? QStringLiteral("info") : QStringLiteral("error"));
        if (ok)
            refresh();
    });
}

bool Backend::saveSettings(const QVariantMap &values)
{
    Settings s = m_app->settings();
    QString error;
    if (!SettingsSchema::apply(s, values, &error)) {
        emit toast(error, QStringLiteral("error"));
        return false;
    }
    m_app->applySettings(s);
    emit settingsChanged();
    emit toast(QStringLiteral("Configurações salvas."), QStringLiteral("info"));
    return true;
}

void Backend::uploadToYouTube(const QString &path, const QVariantMap &video)
{
    const QString v = safeVideo(path);
    if (v.isEmpty())
        return;
    YouTube::Video meta;
    meta.title = video.value(QStringLiteral("title")).toString();
    meta.description = video.value(QStringLiteral("description")).toString();
    meta.tags = YouTube::parseTags(video.value(QStringLiteral("tags")).toString());
    const QString privacy = video.value(QStringLiteral("privacy")).toString();
    if (privacy == QLatin1String("public") || privacy == QLatin1String("private"))
        meta.privacy = privacy;
    meta.madeForKids = video.value(QStringLiteral("madeForKids")).toBool();
    m_app->youtube()->upload(v, meta);
}

void Backend::copyText(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
    emit toast(QStringLiteral("Link copiado."), QStringLiteral("info"));
}

void Backend::openYouTube(const QString &url)
{
    const QUrl u(url);
    if (u.scheme() == QLatin1String("https") && (u.host() == QLatin1String("youtu.be") || u.host().endsWith(QLatin1String("youtube.com"))))
        QDesktopServices::openUrl(u);
}

void Backend::saveClipNow() { m_app->saveManualClip(); }

void Backend::toggleRecording() { m_app->toggleRecording(); }

QString Backend::folderUrl(const QString &path) const { return QUrl::fromLocalFile(path).toString(); }

QString Backend::pathFromUrl(const QUrl &url) const { return QDir::toNativeSeparators(url.toLocalFile()); }

} // namespace aeonis
