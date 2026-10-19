#pragma once
// Grava o vídeo e o áudio codificados em segmentos MPEG-TS de ~2 s (seg_000000.ts, ...), trocando de
// arquivo a cada keyframe depois de 2 s. É o "buffer em disco": barato, sobrevive a crash e permite
// salvar clipes por concatenação sem re-encode. Thread-safe (vídeo e áudio chegam de threads diferentes).

#include <QString>

#include <mutex>

struct AVCodecContext;
struct AVFormatContext;
struct AVPacket;
struct AVRational;

namespace aeonis {

class SegmentWriter {
public:
    static constexpr int SegmentSeconds = 2;

    SegmentWriter() = default;
    ~SegmentWriter();

    /// `audio` pode ser nulo. Os contextos precisam estar abertos (parâmetros finais).
    bool open(const QString &dir, int firstIndex, AVCodecContext *video, AVCodecContext *audio, QString *error);
    void close();

    /// Pacote do encoder de vídeo (time_base do encoder).
    void writeVideo(AVPacket *pkt);
    /// Pacote do encoder de áudio (time_base do encoder). Ignorado até o primeiro keyframe de vídeo.
    void writeAudio(AVPacket *pkt);

    /// Índice do segmento sendo escrito agora (-1 antes do primeiro).
    int currentIndex() const;
    qint64 bytesWritten() const;

private:
    bool startSegment(QString *error);
    void finishSegment();

    mutable std::mutex m_mutex;
    QString m_dir;
    int m_index = -1;
    int m_nextIndex = 0;
    AVCodecContext *m_video = nullptr;
    AVCodecContext *m_audio = nullptr;
    AVFormatContext *m_fmt = nullptr;
    int64_t m_segmentStart = 0; // pts de vídeo (time_base do encoder) do início do segmento
    bool m_started = false;
    qint64 m_bytes = 0;
};

} // namespace aeonis
