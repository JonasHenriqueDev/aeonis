#pragma once
// Eventos de uma partida de Deadlock depois que ela acaba. O jogo não tem API ao vivo e o console.log
// não registra abates; a deadlock-api.com (comunidade, não da Valve) publica o metadata da partida alguns
// minutos depois do fim: mortes de cada jogador (com quem matou), objetivos, chefe do meio e pausas,
// tudo em segundos do relógio da partida. Só o id da partida (do console.log) vai na consulta.

#include "EventSources.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

class QNetworkAccessManager;

namespace aeonis {

namespace DeadlockMatch {

/// SteamID64 -> account id (32 bits), como a API identifica os jogadores.
qint64 accountId(quint64 steamId64);

struct Event {
    double gameTime = 0; // segundos no relógio da partida
    QString label;
    bool highlight = false; // vira clipe (abate, multikill; morte se a opção estiver ligada)
};

struct Parsed {
    bool ok = false; // achou o jogador na partida
    int heroId = 0;
    QJsonObject info; // kills, deaths, assists, level, cs, result, mode (sem o nome do herói)
    QList<Event> events; // em ordem de tempo
    QList<std::pair<double, double>> pauses; // (instante no relógio, duração em s)
};

/// Lê o JSON de /v1/matches/{id}/metadata para o jogador `accountId`.
Parsed parse(const QJsonObject &metadata, qint64 accountId, const EventOptions &options);
/// Instante real de `gameTime`: o relógio começa em `clockZero` (GameInProgress no log) e para nas pausas.
QDateTime wallTime(const QDateTime &clockZero, double gameTime, const QList<std::pair<double, double>> &pauses);
/// Nome do objetivo destruído (`team_objective_id`); vazio = não marcar.
QString objectiveName(int id);

} // namespace DeadlockMatch

/// Consulta a API até o metadata aparecer (ou desistir) e entrega os eventos com o instante real.
class DeadlockMatchFetcher : public QObject {
    Q_OBJECT
public:
    static constexpr int FirstDelaySeconds = 60;
    static constexpr int RetrySeconds = 90;
    static constexpr int MaxAttempts = 25; // ~37 min

    struct Job {
        qint64 matchId = 0;
        qint64 accountId = 0;
        QDateTime clockZero;
        QString session; // sessão do Aeonis com o vídeo da partida
        int attempts = 0;
    };
    struct Result {
        Job job;
        QJsonObject info; // com champion/championId quando o herói é conhecido
        QList<std::pair<QDateTime, DeadlockMatch::Event>> events;
    };

    explicit DeadlockMatchFetcher(EventSource::Options options, QObject *parent = nullptr);
    void add(const Job &job);

signals:
    void ready(const aeonis::DeadlockMatchFetcher::Result &result);

private:
    void schedule(const Job &job, int seconds);
    void fetch(Job job);
    void finish(const Job &job, const DeadlockMatch::Parsed &parsed);

    EventSource::Options m_options;
    QNetworkAccessManager *m_net;
    QSet<qint64> m_seen; // partidas já pedidas
};

} // namespace aeonis
