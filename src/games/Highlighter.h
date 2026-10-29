#pragma once
// Junta eventos próximos (ex.: abate + double kill + triple kill) em um único clipe: cada evento
// empurra o fim do clipe para "agora + segundos depois", limitado a 60 s desde o primeiro evento.

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <utility>

namespace aeonis {

class Highlighter : public QObject {
    Q_OBJECT
public:
    static constexpr int MaxExtendSeconds = 60;
    /// (pré, pós) em segundos, lidos das configurações a cada evento.
    using Timing = std::function<std::pair<int, int>()>;
    using Save = std::function<void(int seconds, const QString &label)>;

    Highlighter(Timing timing, Save save, QObject *parent = nullptr);

    void trigger(const QString &reason);
    /// Salva imediatamente um destaque pendente (ex.: o jogo fechou antes do fim da janela).
    void flush();
    bool pending() const { return m_first.isValid(); }

    /// Rótulo do clipe a partir dos eventos (o mais importante; "3 Abates" para abates simples).
    static QString label(const QStringList &reasons);

    /// Trecho de um vídeo já gravado para um grupo de eventos (clipes sugeridos).
    struct Span {
        double start = 0;
        double end = 0;
        QStringList reasons;
    };
    /// Eventos (instante em s, rótulo) de um vídeo viram trechos [primeiro − pré, último + pós], juntando os
    /// próximos como no destaque ao vivo: cabem no mesmo clipe enquanto as janelas se tocam e o grupo
    /// não passa de MaxExtendSeconds. `duration` > 0 limita o fim.
    static QList<Span> spans(QList<std::pair<double, QString>> events, int pre, int post, double duration = 0);

private:
    void fire();

    Timing m_timing;
    Save m_save;
    QTimer m_timer;
    QStringList m_reasons;
    QDateTime m_first;
    QDateTime m_deadline;
};

} // namespace aeonis
