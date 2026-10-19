#pragma once
// Fontes de eventos que disparam destaques. Usam só APIs oficiais e locais dos próprios jogos:
//  - League of Legends: Live Client Data API (https://127.0.0.1:2999), durante a partida.
//  - Counter-Strike 2: Game State Integration; o Aeonis instala um .cfg e recebe POSTs em 127.0.0.1:3005.

#include <QJsonArray>
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
    /// Evento que vira clipe (e marcador na linha do tempo).
    void highlight(const QString &reason);
    /// Evento só para a linha do tempo (objetivo do time, morte sem clipe...).
    void marker(const QString &label);
    /// O relógio da partida começou (fim da tela de carregamento); `gameTime` em segundos.
    void clockStarted(double gameTime);
    /// Campeão, KDA, modo e resultado da partida (emitido quando muda).
    void matchInfo(const QJsonObject &info);

protected:
    Options m_options;
};

class LeagueEvents : public EventSource {
    Q_OBJECT
public:
    /// `exePath`: ...\League of Legends\Game\League of Legends.exe (acha o lockfile do cliente).
    LeagueEvents(Options options, const QString &exePath, QObject *parent = nullptr);
    void start() override;

    /// Destaque de um evento da Live Client Data API para o jogador `me` (vazio = nenhum).
    static QString classify(const QJsonObject &event, const QString &me, const EventOptions &options);
    /// Marcador da linha do tempo: tudo que envolve o jogador e os objetivos do time dele
    /// (`team`: ORDER | CHAOS, `allies`: nomes do time). Vazio = nada a marcar.
    static QString timelineLabel(const QJsonObject &event, const QString &me, const QString &team, const QStringList &allies);
    /// Campeão, KDA, CS, nível e modo do jogador a partir do /allgamedata.
    static QJsonObject playerInfo(const QJsonObject &allGameData, const QString &me);
    /// Nome da fila (ranqueada, normal, ARAM...) pelo id do cliente; vazio se desconhecida.
    static QString queueName(int queueId);
    static QString modeName(const QString &gameMode);

private:
    void poll();
    void handleAll(const QByteArray &body);
    void handleEvents(const QJsonArray &events);
    void fetchQueue();
    void fetchChampionIcon(const QString &championId);
    void publish();

    QNetworkAccessManager *m_net = nullptr;
    QTimer m_timer;
    QString m_exe;
    QString m_me;
    QString m_team;
    QStringList m_allies;
    QJsonObject m_info, m_published;
    int m_lastId = INT_MIN;
    int m_tick = 0;
    bool m_busy = false;
    bool m_clock = false;
    bool m_iconRequested = false;
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
