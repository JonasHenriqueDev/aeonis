#include "YouTube.h"
#include "core/Log.h"
#include "core/Paths.h"

#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>

#include <windows.h>
#include <wincred.h>

// Cliente OAuth embutido na compilação (CMake: AEONIS_YOUTUBE_CLIENT_ID/SECRET). Em apps de
// computador o "secret" não é confidencial, mas fica fora do repositório.
#ifndef AEONIS_YOUTUBE_CLIENT_ID
#define AEONIS_YOUTUBE_CLIENT_ID ""
#endif
#ifndef AEONIS_YOUTUBE_CLIENT_SECRET
#define AEONIS_YOUTUBE_CLIENT_SECRET ""
#endif

namespace aeonis {

namespace {

constexpr const wchar_t *CredentialTarget = L"Aeonis/YouTube";
constexpr const char *Scope = "https://www.googleapis.com/auth/youtube.upload";
constexpr const char *AuthUrl = "https://accounts.google.com/o/oauth2/v2/auth";
constexpr const char *TokenUrl = "https://oauth2.googleapis.com/token";
constexpr const char *UploadUrl = "https://www.googleapis.com/upload/youtube/v3/videos?uploadType=resumable&part=snippet,status";
constexpr qint64 ChunkSize = 8 * 1024 * 1024; // múltiplo de 256 KB, como a API exige
constexpr int MaxRetries = 6;

QString readCredential()
{
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(CredentialTarget, CRED_TYPE_GENERIC, 0, &cred))
        return {};
    const QString token = QString::fromUtf8(reinterpret_cast<const char *>(cred->CredentialBlob), cred->CredentialBlobSize);
    CredFree(cred);
    return token;
}

bool writeCredential(const QString &token)
{
    QByteArray blob = token.toUtf8();
    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(CredentialTarget);
    cred.UserName = const_cast<LPWSTR>(L"Aeonis");
    cred.CredentialBlobSize = DWORD(blob.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(blob.data());
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    return CredWriteW(&cred, 0);
}

void deleteCredential() { CredDeleteW(CredentialTarget, CRED_TYPE_GENERIC, 0); }

QByteArray randomToken(int bytes)
{
    QByteArray raw(bytes, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32 *>(raw.data()), bytes / 4);
    return raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QByteArray formEncode(const QList<QPair<QString, QString>> &items)
{
    QByteArray out;
    for (const auto &[k, v] : items) {
        if (!out.isEmpty())
            out += '&';
        out += QUrl::toPercentEncoding(k) + '=' + QUrl::toPercentEncoding(v);
    }
    return out;
}

int httpStatus(QNetworkReply *reply) { return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); }

/// Mensagem legível de um erro da API (corpo JSON do Google).
QString apiError(const QByteArray &body, int status)
{
    const QJsonObject error = QJsonDocument::fromJson(body).object().value(QLatin1String("error")).toObject();
    const QString reason = error.value(QLatin1String("errors")).toArray().first().toObject().value(QLatin1String("reason")).toString();
    if (reason == QLatin1String("quotaExceeded"))
        return QStringLiteral("A cota diária da API do YouTube acabou. Tente de novo amanhã.");
    if (reason == QLatin1String("uploadLimitExceeded"))
        return QStringLiteral("Sua conta atingiu o limite de envios do YouTube por hoje.");
    if (reason == QLatin1String("youtubeSignupRequired"))
        return QStringLiteral("Essa conta do Google ainda não tem um canal no YouTube. Crie o canal e tente de novo.");
    const QString message = error.value(QLatin1String("message")).toString();
    return message.isEmpty() ? QStringLiteral("O YouTube recusou o envio (HTTP %1).").arg(status)
                             : QStringLiteral("O YouTube recusou o envio: %1").arg(message);
}

QByteArray authPage(bool ok, const QString &text)
{
    const QString html = QStringLiteral(
        "<!doctype html><html lang=\"pt-BR\"><meta charset=\"utf-8\"><title>Aeonis</title>"
        "<body style=\"background:#0e0f13;color:#ecebf3;font-family:'Segoe UI',sans-serif;display:flex;"
        "align-items:center;justify-content:center;height:100vh;margin:0\"><div style=\"text-align:center\">"
        "<h1 style=\"color:%1\">%2</h1><p>%3</p></div></body></html>")
        .arg(ok ? QStringLiteral("#22e5f5") : QStringLiteral("#f43f5e"),
             ok ? QStringLiteral("Pronto!") : QStringLiteral("Não deu certo"), text.toHtmlEscaped());
    const QByteArray body = html.toUtf8();
    return QByteArrayLiteral("HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nConnection: close\r\nContent-Length: ")
        + QByteArray::number(body.size()) + "\r\n\r\n" + body;
}

QString uploadsFile() { return Paths::appData() + QStringLiteral("/youtube.json"); }

} // namespace

struct YouTube::Job {
    QString path;
    Video video;
    QFile file;
    qint64 size = 0;
    qint64 sent = 0; // bytes confirmados pelo YouTube
    QUrl session;
    int retries = 0;
    bool refreshed = false;
};

YouTube::YouTube(const QString &clientId, const QString &clientSecret, QObject *parent)
    : QObject(parent),
      m_clientId(clientId.isEmpty() ? QStringLiteral(AEONIS_YOUTUBE_CLIENT_ID) : clientId),
      m_clientSecret(clientId.isEmpty() ? QStringLiteral(AEONIS_YOUTUBE_CLIENT_SECRET) : clientSecret)
{
    m_refreshToken = readCredential();
    m_authTimeout.setSingleShot(true);
    m_authTimeout.setInterval(5 * 60 * 1000);
    connect(&m_authTimeout, &QTimer::timeout, this, [this] { finishAuth({}, QStringLiteral("timeout")); });
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, [this] {
        if (m_job)
            m_job->session.isEmpty() ? withToken([this](bool ok, const QString &e) { ok ? startSession() : finishUpload(false, {}, e); })
                                     : queryStatus();
    });

    QFile f(uploadsFile());
    if (f.open(QIODevice::ReadOnly))
        m_uploads = QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
}

YouTube::~YouTube()
{
    if (m_reply)
        m_reply->abort();
}

void YouTube::setStatus(const QString &status)
{
    m_status = status;
    emit changed();
}

QString YouTube::uploadPath() const { return m_job ? m_job->path : QString(); }

QString YouTube::key(const QString &path) { return QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()).toLower(); }

QString YouTube::uploadedUrl(const QString &path) const
{
    return m_uploads.value(key(path)).toMap().value(QStringLiteral("url")).toString();
}

void YouTube::saveUploads() const
{
    QSaveFile f(uploadsFile());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject::fromVariantMap(m_uploads)).toJson());
        f.commit();
    }
}

// ---------------------------------------------------------------- regras do YouTube

QString YouTube::sanitizeTitle(const QString &title)
{
    QString t = title.simplified();
    t.replace(QLatin1Char('<'), QChar(0x2039)).replace(QLatin1Char('>'), QChar(0x203A)); // < > são proibidos
    if (t.size() > 100)
        t = t.left(100).trimmed();
    return t.isEmpty() ? QStringLiteral("Clipe do Aeonis") : t;
}

QString YouTube::sanitizeDescription(const QString &description)
{
    QString d = description.trimmed();
    d.replace(QLatin1Char('<'), QChar(0x2039)).replace(QLatin1Char('>'), QChar(0x203A));
    while (d.toUtf8().size() > 5000) // o limite é em bytes
        d.chop(1);
    return d;
}

QStringList YouTube::parseTags(const QString &commaSeparated)
{
    QStringList out;
    int total = 0;
    for (QString tag : commaSeparated.split(QLatin1Char(','))) {
        tag = tag.simplified().remove(QLatin1Char('<')).remove(QLatin1Char('>'));
        if (tag.isEmpty() || out.contains(tag, Qt::CaseInsensitive))
            continue;
        // A API soma 500 caracteres no total; tags com espaço contam as aspas.
        const int cost = int(tag.size()) + (tag.contains(QLatin1Char(' ')) ? 2 : 0) + (out.isEmpty() ? 0 : 1);
        if (total + cost > 500)
            break;
        total += cost;
        out << tag;
    }
    return out;
}

QByteArray YouTube::pkceChallenge(const QByteArray &verifier)
{
    return QCryptographicHash::hash(verifier, QCryptographicHash::Sha256)
        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

qint64 YouTube::confirmedBytes(const QByteArray &range)
{
    const int dash = range.lastIndexOf('-');
    if (dash < 0)
        return 0;
    bool ok = false;
    const qint64 last = range.mid(dash + 1).trimmed().toLongLong(&ok);
    return ok ? last + 1 : 0;
}

// ---------------------------------------------------------------- login

void YouTube::connectAccount()
{
    if (!configured() || connecting())
        return;
    m_server = new QTcpServer(this);
    if (!m_server->listen(QHostAddress::LocalHost, 0)) {
        delete m_server;
        m_server = nullptr;
        emit connectFinished(false, QStringLiteral("Não foi possível abrir a porta local para o login."));
        return;
    }
    connect(m_server, &QTcpServer::newConnection, this, &YouTube::handleAuthSocket);
    m_verifier = randomToken(48);
    m_state = randomToken(16);
    m_redirect = QStringLiteral("http://127.0.0.1:%1").arg(m_server->serverPort());
    const QByteArray query = formEncode({{QStringLiteral("client_id"), m_clientId},
                                         {QStringLiteral("redirect_uri"), m_redirect},
                                         {QStringLiteral("response_type"), QStringLiteral("code")},
                                         {QStringLiteral("scope"), QLatin1String(Scope)},
                                         {QStringLiteral("code_challenge"), QString::fromLatin1(pkceChallenge(m_verifier))},
                                         {QStringLiteral("code_challenge_method"), QStringLiteral("S256")},
                                         {QStringLiteral("state"), QString::fromLatin1(m_state)},
                                         {QStringLiteral("access_type"), QStringLiteral("offline")},
                                         {QStringLiteral("prompt"), QStringLiteral("consent")}});
    m_authTimeout.start();
    setStatus(QStringLiteral("Autorize o Aeonis na janela do navegador…"));
    QDesktopServices::openUrl(QUrl::fromEncoded(QByteArray(AuthUrl) + '?' + query));
}

void YouTube::cancelConnect()
{
    if (m_server)
        finishAuth({}, QStringLiteral("cancelled"));
}

void YouTube::handleAuthSocket()
{
    while (m_server && m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            QByteArray data = socket->property("buffer").toByteArray() + socket->readAll();
            socket->setProperty("buffer", data);
            if (!data.contains("\r\n\r\n") && data.size() < 16 * 1024)
                return;
            // "GET /?code=...&state=... HTTP/1.1"
            const QList<QByteArray> line = data.left(data.indexOf("\r\n")).split(' ');
            const QUrlQuery q(QUrl::fromEncoded("http://127.0.0.1" + line.value(1)));
            const QString code = q.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded);
            const QString error = q.queryItemValue(QStringLiteral("error"), QUrl::FullyDecoded);
            if (code.isEmpty() && error.isEmpty()) { // favicon e afins
                socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                socket->disconnectFromHost();
                return;
            }
            const bool stateOk = q.queryItemValue(QStringLiteral("state")).toLatin1() == m_state;
            const bool ok = stateOk && !code.isEmpty();
            socket->write(authPage(ok, ok ? QStringLiteral("O Aeonis está conectado ao YouTube. Pode fechar esta aba.")
                                          : QStringLiteral("O login no YouTube foi cancelado. Pode fechar esta aba.")));
            socket->disconnectFromHost();
            finishAuth(ok ? code : QString(), ok ? QString() : (stateOk ? error : QStringLiteral("state")));
        });
    }
}

void YouTube::finishAuth(const QString &code, const QString &error)
{
    m_authTimeout.stop();
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    if (code.isEmpty()) {
        setStatus({});
        if (error != QLatin1String("cancelled"))
            Log::warn(QStringLiteral("YouTube: login não concluído (%1)").arg(error));
        emit connectFinished(false, error == QLatin1String("timeout") ? QStringLiteral("O login no YouTube expirou.")
                                    : error == QLatin1String("cancelled") ? QString()
                                                                          : QStringLiteral("O login no YouTube foi cancelado."));
        return;
    }
    m_exchanging = true;
    setStatus(QStringLiteral("Conectando ao YouTube…"));
    exchange({{QStringLiteral("grant_type"), QStringLiteral("authorization_code")},
              {QStringLiteral("code"), code},
              {QStringLiteral("redirect_uri"), m_redirect},
              {QStringLiteral("code_verifier"), QString::fromLatin1(m_verifier)}},
             [this](bool ok, const QString &message) {
                 m_exchanging = false;
                 setStatus({});
                 if (ok)
                     Log::info(QStringLiteral("YouTube: conta conectada"));
                 emit connectFinished(ok, ok ? QStringLiteral("Conta do YouTube conectada.") : message);
             });
}

void YouTube::exchange(const QList<QPair<QString, QString>> &form, std::function<void(bool, const QString &)> then)
{
    QList<QPair<QString, QString>> body{{QStringLiteral("client_id"), m_clientId}};
    if (!m_clientSecret.isEmpty())
        body << QPair<QString, QString>{QStringLiteral("client_secret"), m_clientSecret};
    body << form;
    QNetworkRequest req{QUrl(QLatin1String(TokenUrl))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/x-www-form-urlencoded"));
    req.setTransferTimeout(30000);
    const bool refreshing = form.value(0).second == QLatin1String("refresh_token");
    QNetworkReply *reply = m_net.post(req, formEncode(body));
    connect(reply, &QNetworkReply::finished, this, [this, reply, then, refreshing] {
        reply->deleteLater();
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        const QString access = o.value(QLatin1String("access_token")).toString();
        if (access.isEmpty()) {
            const QString error = o.value(QLatin1String("error")).toString();
            Log::warn(QStringLiteral("YouTube: token recusado (%1 %2)").arg(error, reply->errorString()));
            if (refreshing && error == QLatin1String("invalid_grant")) {
                // Acesso revogado ou expirado: precisa entrar de novo.
                m_refreshToken.clear();
                deleteCredential();
                emit changed();
                then(false, QStringLiteral("A conta do YouTube foi desconectada. Conecte de novo para enviar."));
                return;
            }
            then(false, reply->error() == QNetworkReply::NoError || !error.isEmpty()
                            ? QStringLiteral("O Google recusou o login no YouTube (%1).").arg(error)
                            : QStringLiteral("Sem conexão com o Google (%1).").arg(reply->errorString()));
            return;
        }
        m_accessToken = access;
        m_accessExpires = QDateTime::currentDateTimeUtc().addSecs(o.value(QLatin1String("expires_in")).toInt(3600) - 60);
        if (const QString refresh = o.value(QLatin1String("refresh_token")).toString(); !refresh.isEmpty()) {
            m_refreshToken = refresh;
            if (!writeCredential(refresh))
                Log::warn(QStringLiteral("YouTube: não foi possível guardar o token no Gerenciador de Credenciais"));
            emit changed();
        }
        then(true, {});
    });
}

void YouTube::withToken(std::function<void(bool, const QString &)> then)
{
    if (!m_accessToken.isEmpty() && QDateTime::currentDateTimeUtc() < m_accessExpires) {
        then(true, {});
        return;
    }
    if (m_refreshToken.isEmpty()) {
        then(false, QStringLiteral("Conecte sua conta do YouTube primeiro."));
        return;
    }
    exchange({{QStringLiteral("grant_type"), QStringLiteral("refresh_token")}, {QStringLiteral("refresh_token"), m_refreshToken}},
             std::move(then));
}

void YouTube::disconnectAccount()
{
    if (m_job)
        cancelUpload();
    if (!m_refreshToken.isEmpty()) {
        // Revoga no Google também (sem esperar a resposta).
        QNetworkRequest req{QUrl(QStringLiteral("https://oauth2.googleapis.com/revoke"))};
        req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/x-www-form-urlencoded"));
        QNetworkReply *reply = m_net.post(req, formEncode({{QStringLiteral("token"), m_refreshToken}}));
        connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }
    m_refreshToken.clear();
    m_accessToken.clear();
    deleteCredential();
    Log::info(QStringLiteral("YouTube: conta desconectada"));
    emit changed();
}

// ---------------------------------------------------------------- envio

void YouTube::upload(const QString &path, const Video &video)
{
    if (m_job)
        return;
    auto job = std::make_unique<Job>();
    job->path = path;
    job->video = video;
    job->video.title = sanitizeTitle(video.title);
    job->video.description = sanitizeDescription(video.description);
    job->file.setFileName(path);
    if (!job->file.open(QIODevice::ReadOnly) || job->file.size() == 0) {
        emit uploadFinished(false, path, {}, QStringLiteral("Não foi possível abrir o vídeo para enviar."));
        return;
    }
    job->size = job->file.size();
    m_job = std::move(job);
    m_progress = 0;
    emit progressChanged();
    setStatus(QStringLiteral("Preparando o envio…"));
    Log::info(QStringLiteral("YouTube: enviando %1 (%2 bytes)").arg(path).arg(m_job->size));
    withToken([this](bool ok, const QString &error) { ok ? startSession() : finishUpload(false, {}, error); });
}

void YouTube::startSession()
{
    if (!m_job)
        return;
    const Video &v = m_job->video;
    QJsonObject snippet{{QStringLiteral("title"), v.title},
                        {QStringLiteral("description"), v.description},
                        {QStringLiteral("categoryId"), QStringLiteral("20")}}; // Jogos
    if (!v.tags.isEmpty())
        snippet.insert(QStringLiteral("tags"), QJsonArray::fromStringList(v.tags));
    const QJsonObject meta{{QStringLiteral("snippet"), snippet},
                           {QStringLiteral("status"), QJsonObject{{QStringLiteral("privacyStatus"), v.privacy},
                                                                  {QStringLiteral("selfDeclaredMadeForKids"), v.madeForKids}}}};
    QNetworkRequest req{QUrl(QLatin1String(UploadUrl))};
    req.setRawHeader("Authorization", "Bearer " + m_accessToken.toLatin1());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json; charset=UTF-8"));
    req.setRawHeader("X-Upload-Content-Length", QByteArray::number(m_job->size));
    req.setRawHeader("X-Upload-Content-Type", "video/mp4");
    req.setTransferTimeout(30000);
    setStatus(QStringLiteral("Enviando para o YouTube…"));
    m_reply = m_net.post(req, QJsonDocument(meta).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply.data()] {
        reply->deleteLater();
        if (reply != m_reply || !m_job)
            return;
        m_reply = nullptr;
        const int status = httpStatus(reply);
        const QByteArray location = reply->rawHeader("Location");
        if (status == 200 && !location.isEmpty()) {
            m_job->session = QUrl::fromEncoded(location);
            m_job->retries = 0;
            sendChunk();
        } else if (status == 401 && !m_job->refreshed) {
            m_job->refreshed = true;
            m_accessToken.clear();
            withToken([this](bool ok, const QString &e) { ok ? startSession() : finishUpload(false, {}, e); });
        } else if (status == 0 || status >= 500) {
            retryLater(reply->errorString());
        } else {
            finishUpload(false, {}, apiError(reply->readAll(), status));
        }
    });
}

void YouTube::sendChunk()
{
    if (!m_job)
        return;
    Job &j = *m_job;
    if (!j.file.seek(j.sent)) {
        finishUpload(false, {}, QStringLiteral("Não foi possível ler o vídeo."));
        return;
    }
    const QByteArray chunk = j.file.read(std::min(ChunkSize, j.size - j.sent));
    if (chunk.isEmpty()) {
        finishUpload(false, {}, QStringLiteral("Não foi possível ler o vídeo."));
        return;
    }
    QNetworkRequest req(j.session);
    req.setRawHeader("Authorization", "Bearer " + m_accessToken.toLatin1());
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("video/mp4"));
    req.setRawHeader("Content-Range", QStringLiteral("bytes %1-%2/%3").arg(j.sent).arg(j.sent + chunk.size() - 1).arg(j.size).toLatin1());
    // 308 aqui é "Resume Incomplete", não um redirecionamento.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(60000);
    m_reply = m_net.put(req, chunk);
    connect(m_reply, &QNetworkReply::uploadProgress, this, [this](qint64 sent, qint64) {
        if (!m_job)
            return;
        m_progress = std::min(1.0, double(m_job->sent + sent) / double(m_job->size));
        emit progressChanged();
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply.data()] {
        reply->deleteLater();
        if (reply != m_reply || !m_job)
            return;
        m_reply = nullptr;
        const int status = httpStatus(reply);
        if (status == 308) {
            m_job->sent = confirmedBytes(reply->rawHeader("Range"));
            m_job->retries = 0;
            sendChunk();
        } else if (status == 200 || status == 201) {
            const QJsonObject video = QJsonDocument::fromJson(reply->readAll()).object();
            const QString id = video.value(QLatin1String("id")).toString();
            const QString privacy = video.value(QLatin1String("status")).toObject().value(QLatin1String("privacyStatus")).toString();
            QString message = QStringLiteral("Vídeo enviado para o YouTube.");
            if (privacy == QLatin1String("private") && m_job->video.privacy != QLatin1String("private"))
                message = QStringLiteral("Vídeo enviado, mas o YouTube o deixou privado: o app do Aeonis ainda não foi verificado pelo Google.");
            finishUpload(!id.isEmpty(), id.isEmpty() ? QString() : QStringLiteral("https://youtu.be/") + id,
                         id.isEmpty() ? QStringLiteral("O YouTube não devolveu o link do vídeo.") : message);
        } else if (status == 401 && !m_job->refreshed) {
            m_job->refreshed = true;
            m_accessToken.clear();
            withToken([this](bool ok, const QString &e) { ok ? queryStatus() : finishUpload(false, {}, e); });
        } else if (status == 404 || status == 410) {
            finishUpload(false, {}, QStringLiteral("A sessão de envio do YouTube expirou. Tente de novo."));
        } else if (status == 0 || status >= 500) {
            retryLater(reply->errorString());
        } else {
            finishUpload(false, {}, apiError(reply->readAll(), status));
        }
    });
}

void YouTube::queryStatus()
{
    if (!m_job)
        return;
    QNetworkRequest req(m_job->session);
    req.setRawHeader("Authorization", "Bearer " + m_accessToken.toLatin1());
    req.setRawHeader("Content-Range", QStringLiteral("bytes */%1").arg(m_job->size).toLatin1());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(30000);
    m_reply = m_net.put(req, QByteArray());
    connect(m_reply, &QNetworkReply::finished, this, [this, reply = m_reply.data()] {
        reply->deleteLater();
        if (reply != m_reply || !m_job)
            return;
        m_reply = nullptr;
        const int status = httpStatus(reply);
        if (status == 308) {
            m_job->sent = confirmedBytes(reply->rawHeader("Range"));
            setStatus(QStringLiteral("Enviando para o YouTube…"));
            sendChunk();
        } else if (status == 200 || status == 201) {
            const QString id = QJsonDocument::fromJson(reply->readAll()).object().value(QLatin1String("id")).toString();
            finishUpload(!id.isEmpty(), QStringLiteral("https://youtu.be/") + id, QStringLiteral("Vídeo enviado para o YouTube."));
        } else if (status == 0 || status >= 500) {
            retryLater(reply->errorString());
        } else if (status == 404 || status == 410) {
            finishUpload(false, {}, QStringLiteral("A sessão de envio do YouTube expirou. Tente de novo."));
        } else {
            finishUpload(false, {}, apiError(reply->readAll(), status));
        }
    });
}

void YouTube::retryLater(const QString &reason)
{
    if (!m_job)
        return;
    if (++m_job->retries > MaxRetries) {
        finishUpload(false, {}, QStringLiteral("O envio falhou depois de várias tentativas (%1).").arg(reason));
        return;
    }
    const int seconds = std::min(60, 1 << m_job->retries);
    Log::warn(QStringLiteral("YouTube: falha no envio (%1), tentando de novo em %2 s").arg(reason).arg(seconds));
    setStatus(QStringLiteral("Conexão instável, tentando de novo em %1 s…").arg(seconds));
    m_retry.start(seconds * 1000);
}

void YouTube::cancelUpload()
{
    if (!m_job)
        return;
    QNetworkReply *reply = m_reply.data();
    m_reply = nullptr;
    if (reply) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    finishUpload(false, {}, QStringLiteral("Envio cancelado."));
}

void YouTube::finishUpload(bool ok, const QString &url, const QString &message)
{
    if (!m_job)
        return;
    m_retry.stop();
    const QString path = m_job->path;
    if (ok) {
        m_uploads.insert(key(path), QVariantMap{{QStringLiteral("url"), url},
                                                {QStringLiteral("title"), m_job->video.title},
                                                {QStringLiteral("at"), QDateTime::currentDateTime().toString(Qt::ISODate)}});
        saveUploads();
        Log::info(QStringLiteral("YouTube: %1 enviado (%2)").arg(path, url));
    } else {
        Log::warn(QStringLiteral("YouTube: envio de %1 não concluído: %2").arg(path, message));
    }
    m_job.reset();
    m_progress = ok ? 1 : 0;
    emit progressChanged();
    setStatus({});
    emit uploadFinished(ok, path, url, message);
}

} // namespace aeonis
