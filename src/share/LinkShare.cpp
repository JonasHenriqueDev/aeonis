#include "LinkShare.h"
#include "core/Async.h"
#include "core/Log.h"
#include "core/Media.h"
#include "core/Paths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>

namespace aeonis {

namespace {

constexpr const char *ApiUrl = "https://qu.ax/upload";
constexpr const char *Host = "qu.ax";

QString linksFile() { return Paths::appData() + QStringLiteral("/links.json"); }

QString shareDir() { return Paths::appData() + QStringLiteral("/share"); }

QString stamp(const QDateTime &t) { return t.toUTC().toString(Qt::ISODateWithMs); }

} // namespace

LinkShare::LinkShare(QObject *parent) : QObject(parent)
{
    QFile f(linksFile());
    if (f.open(QIODevice::ReadOnly))
        m_links = QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
    // Sobras de uma execução fechada no meio da compressão.
    QDir(shareDir()).removeRecursively();
}

LinkShare::~LinkShare()
{
    if (m_cancelCompress)
        *m_cancelCompress = true;
    if (QNetworkReply *reply = m_reply.data()) {
        reply->disconnect(this); // sem finish() com o App já desmontando
        reply->abort();
    }
    if (!m_temp.isEmpty())
        QFile::remove(m_temp);
}

QString LinkShare::key(const QString &path) { return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).toLower(); }

bool LinkShare::entryValid(const QVariantMap &entry, qint64 size, const QDateTime &modified)
{
    return QUrl(entry.value(QStringLiteral("url")).toString()).host() == QLatin1String(Host)
        && entry.value(QStringLiteral("size")).toLongLong() == size
        && entry.value(QStringLiteral("modified")).toString() == stamp(modified);
}

QByteArray LinkShare::fingerprint(const QString &path)
{
    constexpr qint64 Chunk = 1024 * 1024;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const qint64 size = f.size();
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(QByteArray::number(size));
    hash.addData(f.read(Chunk));
    if (size > Chunk && f.seek(std::max(Chunk, size - Chunk)))
        hash.addData(f.read(Chunk));
    return hash.result().toHex();
}

QByteArray LinkShare::cachedFingerprint(const QString &path) const
{
    // linkFor roda em bindings do QML: só relê o arquivo se ele mudou.
    const QFileInfo fi(path);
    const QString version = QString::number(fi.size()) + QLatin1Char('|') + stamp(fi.lastModified());
    auto it = m_fpCache.find(key(path));
    if (it != m_fpCache.end() && it->first == version)
        return it->second;
    const QByteArray fp = fingerprint(path);
    if (m_fpCache.size() > 256)
        m_fpCache.clear();
    m_fpCache.insert(key(path), {version, fp});
    return fp;
}

QString LinkShare::linkFor(const QString &path) const
{
    const QFileInfo fi(path);
    if (!fi.exists())
        return {};
    const QVariantMap entry = m_links.value(key(path)).toMap();
    if (entryValid(entry, fi.size(), fi.lastModified()))
        return entry.value(QStringLiteral("url")).toString();
    // Enviado antes com outro caminho ou outra data (movido, renomeado, copiado): procura pelo conteúdo.
    bool any = false;
    for (auto it = m_links.cbegin(); it != m_links.cend() && !any; ++it)
        any = it->toMap().value(QStringLiteral("size")).toLongLong() == fi.size();
    if (!any)
        return {};
    const QByteArray fp = cachedFingerprint(path);
    if (fp.isEmpty())
        return {};
    for (auto it = m_links.cbegin(); it != m_links.cend(); ++it) {
        const QVariantMap e = it->toMap();
        const QString url = e.value(QStringLiteral("url")).toString();
        if (e.value(QStringLiteral("size")).toLongLong() == fi.size() && QUrl(url).host() == QLatin1String(Host)
            && e.value(QStringLiteral("fp")).toByteArray() == fp)
            return url;
    }
    return {};
}

QString LinkShare::parseResponse(const QByteArray &body)
{
    // {"success":true,"files":[{"url":"https://qu.ax/iG3Ah", ...}]}
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    if (!root.value(QLatin1String("success")).toBool())
        return {};
    const QString text = root.value(QLatin1String("files")).toArray().first().toObject().value(QLatin1String("url")).toString();
    static const QRegularExpression id(QStringLiteral("^/[A-Za-z0-9_-]+(\\.[A-Za-z0-9]+)?$"));
    const QUrl url(text, QUrl::StrictMode);
    if (!url.isValid() || url.scheme() != QLatin1String("https") || url.host() != QLatin1String(Host)
        || !id.match(url.path()).hasMatch() || url.hasQuery() || url.hasFragment())
        return {};
    return url.toString();
}

void LinkShare::saveLinks() const
{
    QSaveFile f(linksFile());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject::fromVariantMap(m_links)).toJson());
        f.commit();
    }
}

void LinkShare::share(const QString &path)
{
    if (busy()) {
        if (key(path) == key(m_path))
            return; // clique repetido no vídeo que já está sendo enviado
        emit finished(false, path, {}, QStringLiteral("Espere o envio atual terminar."), true); // sem aviso na bandeja
        return;
    }
    if (const QString url = linkFor(path); !url.isEmpty()) {
        emit finished(true, path, url, QStringLiteral("Link copiado."), true);
        return;
    }
    m_path = path;
    ++m_job;
    m_progress = 0;
    emit progressChanged();
    const qint64 size = QFileInfo(path).size();
    if (!needsCompression(size)) {
        upload(path);
        return;
    }

    // Grande demais para o qu.ax: comprime para um arquivo temporário e envia esse.
    m_compressing = true;
    emit changed();
    const quint64 job = m_job;
    QDir().mkpath(shareDir());
    const QString temp = QDir::toNativeSeparators(shareDir() + QStringLiteral("/link-%1.mp4").arg(job));
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    m_cancelCompress = cancel;
    Log::info(QStringLiteral("Link: %1 tem %2 MB, comprimindo antes do envio.").arg(path).arg(size / 1e6, 0, 'f', 0));
    runAsync(this, [path, temp, cancel] {
        const double secs = Media::duration(path);
        if (secs <= 0)
            return Media::RemuxResult{false, QStringLiteral("duração do vídeo desconhecida")};
        Media::RemuxResult r = Media::compressTo(path, temp, 0, secs, CompressLimit, cancel.get());
        if (*cancel) // cancelado (ou o app fechou): não deixa o temporário para trás
            QFile::remove(temp);
        return r;
    }, [this, job, temp](Media::RemuxResult r) {
        if (job != m_job) { // envio cancelado enquanto comprimia
            QFile::remove(temp);
            return;
        }
        m_cancelCompress.reset();
        m_compressing = false;
        if (!r.ok) {
            QFile::remove(temp);
            Log::error(QStringLiteral("Link: falha ao comprimir %1: %2").arg(m_path, r.error));
            finish(false, {}, QStringLiteral("Falha ao comprimir o vídeo (veja o log)."));
            return;
        }
        if (QFileInfo(temp).size() > UploadLimit) {
            QFile::remove(temp);
            finish(false, {}, QStringLiteral("Mesmo comprimido o vídeo passa de 250 MB. Corte um trecho e compartilhe o corte."));
            return;
        }
        m_temp = temp;
        emit changed();
        upload(temp);
    });
}

void LinkShare::upload(const QString &file)
{
    auto *body = new QFile(file);
    if (!body->open(QIODevice::ReadOnly)) {
        delete body;
        finish(false, {}, QStringLiteral("Não foi possível abrir o vídeo."));
        return;
    }
    auto *form = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart expiry; // -1 = "permanente"
    expiry.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"expiry\""));
    expiry.setBody("-1");
    form->append(expiry);
    QHttpPart filePart;
    // Nome ASCII fixo: o qu.ax troca o nome de qualquer jeito e só olha a extensão.
    const QString suffix = QFileInfo(file).suffix().toLower();
    const QString ext = suffix.isEmpty() || suffix.contains(QRegularExpression(QStringLiteral("[^a-z0-9]"))) ? QStringLiteral("mp4") : suffix;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"files[]\"; filename=\"aeonis.%1\"").arg(ext));
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, ext == QLatin1String("mp4") ? "video/mp4" : "application/octet-stream");
    filePart.setBodyDevice(body);
    body->setParent(form);
    form->append(filePart);

    QNetworkRequest req{QUrl(QString::fromLatin1(ApiUrl))};
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Aeonis/" AEONIS_VERSION));
    req.setTransferTimeout(120'000); // sem nenhum byte por 2 min = conexão travada
    m_reply = m_net.post(req, form);
    form->setParent(m_reply);
    emit changed();

    connect(m_reply, &QNetworkReply::uploadProgress, this, [this](qint64 sent, qint64 total) {
        if (total > 0) {
            m_progress = std::min(0.99, double(sent) / double(total));
            emit progressChanged();
        }
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply.data()] {
        reply->deleteLater();
        if (reply != m_reply)
            return;
        m_reply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        if (reply->error() != QNetworkReply::NoError && status == 0) {
            Log::warn(QStringLiteral("Link: falha de rede: %1").arg(reply->errorString()));
            finish(false, {}, reply->error() == QNetworkReply::OperationCanceledError
                                  ? QStringLiteral("O envio ficou parado e foi interrompido. Tente de novo.")
                                  : QStringLiteral("Sem conexão com o qu.ax. Tente de novo."));
            return;
        }
        const QString url = parseResponse(data);
        if (url.isEmpty()) {
            const QString answer = QString::fromUtf8(data.left(200)).simplified();
            Log::warn(QStringLiteral("Link: qu.ax respondeu HTTP %1: %2").arg(status).arg(answer));
            finish(false, {}, answer.isEmpty() || answer.startsWith(QLatin1Char('<'))
                                  ? QStringLiteral("O qu.ax recusou o envio (HTTP %1).").arg(status)
                                  : QStringLiteral("O qu.ax recusou o envio: %1").arg(answer));
            return;
        }
        finish(true, url, QStringLiteral("Link copiado."));
    });
}

void LinkShare::cancel()
{
    if (!busy())
        return;
    ++m_job; // a compressão em andamento, se houver, é descartada
    if (m_cancelCompress) {
        *m_cancelCompress = true;
        m_cancelCompress.reset();
    }
    if (QNetworkReply *reply = m_reply.data()) {
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    m_compressing = false;
    finish(false, {}, QStringLiteral("Envio cancelado."));
}

void LinkShare::finish(bool ok, const QString &url, const QString &message)
{
    const QString path = m_path;
    const bool compressed = !m_temp.isEmpty();
    if (compressed) {
        QFile::remove(m_temp);
        m_temp.clear();
    }
    if (ok) {
        const QFileInfo fi(path);
        m_links.insert(key(path), QVariantMap{{QStringLiteral("url"), url},
                                              {QStringLiteral("size"), fi.size()},
                                              {QStringLiteral("modified"), stamp(fi.lastModified())},
                                              {QStringLiteral("fp"), QString::fromLatin1(cachedFingerprint(path))},
                                              {QStringLiteral("compressed"), compressed},
                                              {QStringLiteral("at"), QDateTime::currentDateTime().toString(Qt::ISODate)}});
        saveLinks();
        Log::info(QStringLiteral("Link: %1 enviado (%2)").arg(path, url));
    } else {
        Log::warn(QStringLiteral("Link: envio de %1 não concluído: %2").arg(path, message));
    }
    m_path.clear();
    m_progress = ok ? 1 : 0;
    emit progressChanged();
    emit changed();
    emit finished(ok, path, url, message, false);
}

} // namespace aeonis
