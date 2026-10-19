#pragma once
// Histórico append-only em %LOCALAPPDATA%\Aeonis\library.jsonl (mesmo formato da versão C#).
// Liga cada vídeo à partida (sessão) e ao jogo de onde veio e guarda os eventos para marcar a
// linha do tempo. Nada é gravado na pasta de vídeos.

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <optional>

namespace aeonis {

struct LibEntry {
    QString t;     // session | end | clip | event | info
    QString s;     // id da sessão
    QDateTime at;
    QString game;
    QString exe;
    QString path;
    QString kind;  // manual | highlight | full
    QString label;
    std::optional<double> duration;
    QDateTime start; // instante do primeiro quadro do clipe (inválido se desconhecido)
    QJsonObject info; // dados da partida (campeão, KDA, modo, resultado); o último "info" da sessão vale

    QByteArray toJsonLine() const;
    static std::optional<LibEntry> fromJsonLine(const QByteArray &line);
};

namespace Library {

void sessionStarted(const QString &session, const QString &game, const QString &exe);
void sessionEnded(const QString &session);
void event(const QString &session, const QString &label);
void matchInfo(const QString &session, const QJsonObject &info);
/// `requestedAt`: momento em que o clipe foi pedido (fim do vídeo). `start`: início conhecido
/// (gravação longa); senão é deduzido de requestedAt − duração.
void clipSaved(const QString &session, const QString &path, const QString &kind, const QString &label,
               const QDateTime &requestedAt, double duration, const QDateTime &start = {});
/// Registra um corte como pertencente à mesma partida do vídeo original.
void derived(const QString &source, const QString &output, double offsetSeconds, double duration);

QList<LibEntry> load();
/// Datas ISO 8601 da versão C# (7 casas decimais, com ou sem fuso).
QDateTime parseDate(const QString &s);
QString formatDate(const QDateTime &dt);

} // namespace Library

} // namespace aeonis
