#pragma once
// "Copiar link": envia o vídeo ao catbox.moe (sem conta; o arquivo só some depois de 2 anos sem acesso) e
// copia o link direto do .mp4, que o Discord e o WhatsApp mostram como vídeo. O catbox aceita até 200 MB:
// acima disso o vídeo é comprimido antes (Media::compressTo, como o "Comprimir" do corte, mirando menos de
// 190 MB) e o arquivo temporário é apagado depois do envio. Os links ficam em links.json e são
// reaproveitados enquanto o arquivo não mudar. O Streamable não serve: a API dele não tem envio oficial e
// o endpoint não documentado exige usuário e senha.

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QVariantMap>

#include <atomic>
#include <memory>

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
    static constexpr qint64 CatboxLimit = 200LL * 1000 * 1000; // "Uploads up to 200 MB"
    static constexpr qint64 CompressLimit = 190LL * 1000 * 1000; // folga para o contêiner e o formulário

    explicit LinkShare(QObject *parent = nullptr);
    ~LinkShare() override;

    bool busy() const { return !m_path.isEmpty(); }
    bool compressing() const { return m_compressing; }
    QString busyPath() const { return m_path; }
    double progress() const { return m_progress; }
    QVariantMap links() const { return m_links; }

    /// Link ainda válido deste arquivo (mesmo tamanho e data); vazio se precisa enviar.
    Q_INVOKABLE QString linkFor(const QString &path) const;
    /// Cancela o envio (e a compressão em andamento).
    Q_INVOKABLE void cancel();

    /// Responde com `finished`: na hora se já há link válido, senão depois do envio. `path` já validado.
    void share(const QString &path);

    // Regras (testadas em tst_core).
    static bool needsCompression(qint64 size) { return size > CatboxLimit; }
    /// Link devolvido pelo catbox (texto puro), ou vazio se a resposta não for um link dele.
    static QString parseResponse(const QByteArray &body);
    /// Entrada de links.json ainda vale para um arquivo com esse tamanho e data?
    static bool entryValid(const QVariantMap &entry, qint64 size, const QDateTime &modified);

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

    QNetworkAccessManager m_net;
    QPointer<QNetworkReply> m_reply;
    QString m_path;      // vídeo original
    QString m_temp;      // arquivo comprimido (apagado no fim e ao fechar)
    bool m_compressing = false;
    quint64 m_job = 0;   // muda a cada envio e cancelamento: descarta compressões antigas
    std::shared_ptr<std::atomic<bool>> m_cancelCompress; // da compressão em andamento
    double m_progress = 0;
    QVariantMap m_links; // caminho normalizado -> { url, size, modified, at, compressed }
};

} // namespace aeonis
