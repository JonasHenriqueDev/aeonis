#pragma once
// "Copiar link": envia o vídeo ao qu.ax (sem conta, armazenamento "permanente": só some depois de muito
// tempo sem acesso) e copia o link da página dele, que traz as tags og:video: o Discord e o WhatsApp
// mostram o vídeo na conversa. O qu.ax aceita até 256 MB: acima de 250 MB o vídeo é comprimido antes
// (Media::compressTo, como o "Comprimir" do corte, mirando menos de 240 MB) e o temporário é apagado depois
// do envio. Os links ficam em links.json e são reaproveitados enquanto o arquivo não mudar; um vídeo já
// enviado alguma vez (mesmo movido ou renomeado) é achado pela impressão digital do conteúdo.
// Hosts descartados: Streamable (sem envio oficial; o endpoint não documentado exige usuário e senha) e
// catbox.moe (responde HEAD com Content-Length: 0 e o Discord não gera o preview).

#include <QDateTime>
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QVariantMap>

#include <atomic>
#include <memory>
#include <utility>

class QNetworkReply;

namespace aeonis {

class LinkShare : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool compressing READ compressing NOTIFY changed)
    Q_PROPERTY(QString busyPath READ busyPath NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QVariantMap links READ links NOTIFY changed)
public:
    static constexpr qint64 UploadLimit = 250LL * 1000 * 1000;   // "Max upload size is 256MB", com folga
    static constexpr qint64 CompressLimit = 240LL * 1000 * 1000; // meta da compressão

    explicit LinkShare(QObject *parent = nullptr);
    ~LinkShare() override;

    bool busy() const { return !m_path.isEmpty(); }
    bool compressing() const { return m_compressing; }
    QString busyPath() const { return m_path; }
    double progress() const { return m_progress; }
    QVariantMap links() const { return m_links; }

    /// Link de um envio anterior deste vídeo: pelo caminho (mesmo tamanho e data) ou, se não houver, pela
    /// impressão digital do conteúdo. Vazio se precisa enviar.
    Q_INVOKABLE QString linkFor(const QString &path) const;
    /// Cancela o envio (e a compressão em andamento).
    Q_INVOKABLE void cancel();

    /// Responde com `finished`: na hora se já há link válido, senão depois do envio. `path` já validado.
    void share(const QString &path);

    // Regras (testadas em tst_core).
    static bool needsCompression(qint64 size) { return size > UploadLimit; }
    /// Link da página do vídeo na resposta JSON do qu.ax, ou vazio se a resposta não trouxer um link dele.
    static QString parseResponse(const QByteArray &body);
    /// Entrada de links.json ainda vale para um arquivo com esse tamanho e data? (links antigos do catbox não)
    static bool entryValid(const QVariantMap &entry, qint64 size, const QDateTime &modified);
    /// SHA-1 (hex) do tamanho + primeiro e último MB do arquivo; vazio se não abrir.
    static QByteArray fingerprint(const QString &path);

signals:
    void changed();
    void progressChanged();
    /// `reused`: o link já existia, nada foi enviado.
    void finished(bool ok, const QString &path, const QString &url, const QString &message, bool reused);

private:
    void upload(const QString &file);
    void finish(bool ok, const QString &url, const QString &message);
    void saveLinks() const;
    static QString key(const QString &path);
    QByteArray cachedFingerprint(const QString &path) const;

    QNetworkAccessManager m_net;
    QPointer<QNetworkReply> m_reply;
    QString m_path;      // vídeo original
    QString m_temp;      // arquivo comprimido (apagado no fim e ao fechar)
    bool m_compressing = false;
    quint64 m_job = 0;   // muda a cada envio e cancelamento: descarta compressões antigas
    std::shared_ptr<std::atomic<bool>> m_cancelCompress; // da compressão em andamento
    double m_progress = 0;
    QVariantMap m_links; // caminho normalizado -> { url, size, modified, fp, at, compressed }
    mutable QHash<QString, std::pair<QString, QByteArray>> m_fpCache; // caminho -> (tamanho+data, impressão)
};

} // namespace aeonis
