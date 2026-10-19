#pragma once
// Uma sessão de captura por jogo: Desktop Duplication -> encoder de hardware -> segmentos MPEG-TS de
// 2 s em disco, com o áudio misturado no mesmo arquivo. A partir desses segmentos, sem re-encode:
//  - replay buffer: o zelador apaga os segmentos mais antigos que a janela necessária;
//  - clipes: concatena os últimos N segmentos em um MP4;
//  - partida completa: "fixa" um segmento inicial e concatena tudo dali até o fim.
// Um só encoder serve para buffer, clipes e gravação longa ao mesmo tempo (como na versão C#).
//
// Os métodos bloqueiam (abrir encoder, remux): chame de uma thread de trabalho. São thread-safe.

#include "core/Settings.h"

#include <QDateTime>
#include <QMutex>
#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <windows.h>

namespace aeonis {

class AudioCapture;
class AudioMixer;
class DesktopDuplication;
class SegmentWriter;
class VideoEncoder;

struct ClipResult {
    QString path;
    double duration = 0;
    explicit operator bool() const { return !path.isEmpty(); }
};

class CaptureEngine : public QObject {
    Q_OBJECT
public:
    static constexpr int SegmentSeconds = 2;
    static constexpr int MaxExtendSeconds = 60; // Highlighter

    explicit CaptureEngine(QObject *parent = nullptr);
    ~CaptureEngine() override;

    bool start(const QString &game, HMONITOR monitor, const Settings &settings, QString *error);
    /// Para a captura; se havia gravação longa, gera o MP4 e devolve o caminho.
    ClipResult stop();
    /// Para sem gerar a gravação longa (encerramento do Windows): ela é recuperada na próxima abertura.
    void stopForShutdown();

    ClipResult saveClip(int seconds, const QString &label);
    void beginRecording();
    ClipResult endRecording();
    /// Atualiza a janela do buffer (configurações mudaram durante a sessão).
    void updateSettings(const Settings &settings);

    bool running() const { return m_running.load(); }
    bool recording() const;
    QString game() const;
    QString sessionId() const;
    QString encoderLabel() const;
    QDateTime recordingSince() const;

    /// Recupera gravações longas interrompidas (queda de energia, crash) e limpa buffers órfãos.
    static void recoverLeftovers(const std::function<void(const ClipResult &, const QString &game)> &recovered);

signals:
    void failed(const QString &message);

private:
    struct Segment {
        int index;
        QString path;
    };
    static std::vector<Segment> listSegments(const QString &dir);
    static ClipResult concat(const std::vector<Segment> &segments, const QString &output);

    void videoLoop();
    void janitorLoop();
    void stopThreads();
    ClipResult finishRecording(const QString &dir);
    bool reopenEncoder(QString *error);

    mutable std::mutex m_mutex; // estado da sessão
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_videoRunning{false};
    std::atomic<int> m_busy{0};
    Settings m_settings;
    QString m_dir;
    QString m_game = QStringLiteral("Desktop");
    QString m_sessionId;
    QString m_encoderLabel = QStringLiteral("-");
    std::optional<int> m_pinStart;
    QString m_pinOutputDir;
    QDateTime m_recordingSince;
    HMONITOR m_monitor = nullptr;
    qint64 m_clockStart = 0;

    std::unique_ptr<DesktopDuplication> m_dup;
    std::unique_ptr<VideoEncoder> m_encoder;
    std::unique_ptr<SegmentWriter> m_writer;
    std::vector<std::unique_ptr<AudioCapture>> m_audio;
    std::unique_ptr<AudioMixer> m_mixer;
    std::thread m_videoThread;
    std::thread m_janitorThread;
    std::mutex m_janitorMutex;
    std::condition_variable m_janitorCv;
    bool m_janitorStop = false;
};

} // namespace aeonis
