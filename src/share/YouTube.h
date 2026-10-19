#pragma once
// Envio de vídeos para o YouTube (YouTube Data API v3). O login abre o navegador e o Google devolve
// o código para um servidor HTTP temporário em 127.0.0.1 (OAuth de app instalado, com PKCE). O token
// de atualização fica no Gerenciador de Credenciais do Windows; o upload é "resumable", em partes de
// 8 MB, e retoma do último byte confirmado se a conexão cair. Os links enviados ficam em youtube.json.

#include <QDateTime>
#include <QFile>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <functional>
#include <memory>

class QNetworkReply;
class QTcpServer;

namespace aeonis {

class YouTube : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool configured READ configured CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool connecting READ connecting NOTIFY changed)
    Q_PROPERTY(bool uploading READ uploading NOTIFY changed)
    Q_PROPERTY(QString uploadPath READ uploadPath NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QVariantMap uploads READ uploads NOTIFY changed)
public:
    struct Video {
        QString title;
        QString description;
        QStringList tags;
        QString privacy = QStringLiteral("unlisted"); // public | unlisted | private
        bool madeForKids = false;
    };

    /// Credenciais do cliente OAuth (tipo "App para computador"); vazias = as embutidas na compilação.
    YouTube(const QString &clientId, const QString &clientSecret, QObject *parent = nullptr);
    ~YouTube() override;

    bool configured() const { return !m_clientId.isEmpty(); }
    bool connected() const { return !m_refreshToken.isEmpty(); }
    bool connecting() const { return m_server != nullptr || m_exchanging; }
    bool uploading() const { return m_job != nullptr; }
    QString uploadPath() const;
    QString status() const { return m_status; }
    double progress() const { return m_progress; }
    QVariantMap uploads() const { return m_uploads; }

    Q_INVOKABLE void connectAccount();
    Q_INVOKABLE void cancelConnect();
    Q_INVOKABLE void disconnectAccount();
    Q_INVOKABLE void cancelUpload();
    /// Link do vídeo já enviado deste arquivo (vazio se nunca foi enviado).
    Q_INVOKABLE QString uploadedUrl(const QString &path) const;

    /// Começa o envio; `path` já validado por quem chama.
    void upload(const QString &path, const Video &video);

    // Regras do YouTube (testadas em tst_core).
    static QString sanitizeTitle(const QString &title);
    static QString sanitizeDescription(const QString &description);
    static QStringList parseTags(const QString &commaSeparated);
    static QByteArray pkceChallenge(const QByteArray &verifier);
    /// Bytes confirmados pelo cabeçalho Range de uma resposta 308 ("bytes=0-1234" → 1235).
    static qint64 confirmedBytes(const QByteArray &range);

signals:
    void changed();
    void progressChanged();
    void connectFinished(bool ok, const QString &message);
    void uploadFinished(bool ok, const QString &path, const QString &url, const QString &message);

private:
    struct Job;

    void setStatus(const QString &status);
    void handleAuthSocket();
    void finishAuth(const QString &code, const QString &error);
    void exchange(const QList<QPair<QString, QString>> &form, std::function<void(bool, const QString &)> then);
    void withToken(std::function<void(bool, const QString &)> then);
    void startSession();
    void sendChunk();
    void queryStatus();
    void retryLater(const QString &reason);
    void finishUpload(bool ok, const QString &url, const QString &message);
    void saveUploads() const;
    static QString key(const QString &path);

    QString m_clientId;
    QString m_clientSecret;
    QNetworkAccessManager m_net;

    // Login
    QTcpServer *m_server = nullptr;
    QTimer m_authTimeout;
    QByteArray m_verifier;
    QByteArray m_state;
    QString m_redirect;
    bool m_exchanging = false;

    // Tokens
    QString m_refreshToken;
    QString m_accessToken;
    QDateTime m_accessExpires;

    // Envio
    std::unique_ptr<Job> m_job;
    QPointer<QNetworkReply> m_reply;
    QTimer m_retry;
    QString m_status;
    double m_progress = 0;
    QVariantMap m_uploads; // caminho normalizado -> { url, title, at }
};

} // namespace aeonis
