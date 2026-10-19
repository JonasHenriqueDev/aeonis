#pragma once
// Atualização pelo GitHub Releases: consulta a última versão, baixa o Aeonis-Setup-x.y.z.exe e roda
// o instalador em modo silencioso (ele reabre o Aeonis no fim). Só fala com api.github.com/github.com.

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QNetworkReply;

namespace aeonis {

class AutoUpdater : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString newVersion READ newVersion NOTIFY changed)
    Q_PROPERTY(qreal progress READ progress NOTIFY changed) // 0..1 no download; -1 = indeterminado
public:
    static constexpr const char *Repository = "JonasHenriqueDev/aeonis";

    explicit AutoUpdater(QObject *parent = nullptr);

    QString version() const;
    QString status() const { return m_status; }
    bool busy() const { return m_busy; }
    bool available() const { return !m_assetUrl.isEmpty(); }
    QString newVersion() const { return m_newVersion; }
    qreal progress() const { return m_progress; }

    /// Já tentou instalar esta versão há pouco e o app continua na antiga (instalador falhou ou
    /// pacote com versão errada): não tenta sozinho de novo, para não entrar em loop.
    bool recentlyTried(const QString &version) const;

    /// Verificação automática: logo ao abrir e a cada 24 h.
    void setAutomatic(bool enabled);

    Q_INVOKABLE void check();
    /// `reopenWindow`: o instalador reabre o Aeonis com a janela (senão, só na bandeja).
    Q_INVOKABLE void downloadAndInstall(bool reopenWindow = true);

    /// "1.2.10" > "1.2.9"; sufixos de pré-release ficam abaixo da versão final.
    static int compareVersions(const QString &a, const QString &b);

signals:
    void changed();
    void updateFound(const QString &version);
    /// Download começou (a tela de atualização aparece) / falhou (ela some).
    void installStarted();
    void installFailed(const QString &reason);
    /// O instalador foi iniciado: o app precisa encerrar (salvando a gravação em andamento).
    void installerLaunched();

private:
    void setStatus(const QString &status, bool busy);
    void fail(const QString &reason);

    QNetworkAccessManager m_net;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timer;
    QString m_status;
    QString m_newVersion;
    QString m_assetUrl;
    QString m_assetName;
    qreal m_progress = -1;
    bool m_busy = false;
    bool m_notified = false;
    int m_retries = 0; // falhas de rede seguidas (tenta de novo em 1, 5 e 15 min)
};

} // namespace aeonis
