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
public:
    static constexpr const char *Repository = "JonasHenriqueDev/aeonis";

    explicit AutoUpdater(QObject *parent = nullptr);

    QString version() const;
    QString status() const { return m_status; }
    bool busy() const { return m_busy; }
    bool available() const { return !m_assetUrl.isEmpty(); }
    QString newVersion() const { return m_newVersion; }

    /// Verificação automática: 1 min depois de abrir e a cada 24 h.
    void setAutomatic(bool enabled);

    Q_INVOKABLE void check();
    Q_INVOKABLE void downloadAndInstall();

    /// "1.2.10" > "1.2.9"; sufixos de pré-release ficam abaixo da versão final.
    static int compareVersions(const QString &a, const QString &b);

signals:
    void changed();
    void updateFound(const QString &version);
    /// O instalador foi iniciado: o app precisa encerrar (salvando a gravação em andamento).
    void installerLaunched();

private:
    void setStatus(const QString &status, bool busy);

    QNetworkAccessManager m_net;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timer;
    QString m_status;
    QString m_newVersion;
    QString m_assetUrl;
    QString m_assetName;
    bool m_busy = false;
    bool m_notified = false;
};

} // namespace aeonis
