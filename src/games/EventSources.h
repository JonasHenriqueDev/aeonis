#pragma once
// Fontes de eventos que disparam destaques. Usam só APIs oficiais e locais dos próprios jogos:
//  - League of Legends: Live Client Data API (https://127.0.0.1:2999), durante a partida.
//  - Counter-Strike 2: Game State Integration; o Aeonis instala um .cfg e recebe POSTs em 127.0.0.1:3005.

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;
class QTcpServer;

namespace aeonis {

struct EventOptions {
    bool deaths = false;
    bool assists = false;
};

class EventSource : public QObject {
    Q_OBJECT
public:
    using Options = std::function<EventOptions()>;
    explicit EventSource(Options options, QObject *parent = nullptr) : QObject(parent), m_options(std::move(options)) {}
    virtual void start() = 0;

signals:
    void highlight(const QString &reason);

protected:
    Options m_options;
};

class LeagueEvents : public EventSource {
    Q_OBJECT
public:
    explicit LeagueEvents(Options options, QObject *parent = nullptr);
    void start() override;

    /// Destaque de um evento da Live Client Data API para o jogador `me` (vazio = nenhum).
    static QString classify(const QJsonObject &event, const QString &me, const EventOptions &options);

private:
    void poll();
    void handleEvents(const QByteArray &body);

    QNetworkAccessManager *m_net = nullptr;
    QTimer m_timer;
    QString m_me;
    int m_lastId = INT_MIN;
    bool m_busy = false;
};

class Cs2Events : public EventSource {
    Q_OBJECT
public:
    static constexpr quint16 Port = 3005;
    static inline const char *Token = "aeonis-gsi";

    /// `exePath`: ...\game\bin\win64\cs2.exe (instala o .cfg em ...\game\csgo\cfg).
    Cs2Events(Options options, const QString &exePath, std::function<void(QString, QString)> notify,
              QObject *parent = nullptr);
    void start() override;

    /// Processa um corpo JSON do GSI (público para os testes).
    void process(const QJsonObject &root);

    static bool ensureConfig(const QString &exePath);

private:
    QTcpServer *m_server = nullptr;
    std::optional<int> m_lastKills, m_lastDeaths;
    QString m_lastMap;
};

} // namespace aeonis
