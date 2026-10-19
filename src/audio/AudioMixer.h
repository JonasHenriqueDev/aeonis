#pragma once
// Junta o áudio do sistema e o microfone em tempo real e codifica em AAC 160 kbps.
// O relógio é o mesmo do vídeo (QueryPerformanceCounter desde o início da sessão): a cada 10 ms
// entrega exatamente as amostras devidas. O WASAPI loopback não entrega nada quando não há som
// tocando, então a fonte em silêncio (ou muito atrasada) é completada com zeros; um atraso
// acumulado é descartado. Assim o áudio não dessincroniza do vídeo (mesma lógica do AudioPipe.cs).

#include "AudioCapture.h"

#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

struct AVCodecContext;
struct AVPacket;

namespace aeonis {

class AudioMixer {
public:
    using Output = std::function<void(AVPacket *)>;

    AudioMixer();
    ~AudioMixer();

    /// Abre o encoder AAC (antes de abrir o gravador de segmentos, que precisa dos parâmetros).
    bool open(QString *error);
    AVCodecContext *context() const { return m_ctx; }

    /// `clockStart`: QueryPerformanceCounter do início da sessão (pts 0).
    void start(std::vector<AudioCapture *> sources, qint64 clockStart, Output out);
    /// Para a thread e esvazia o encoder.
    void stop();

private:
    void run();
    void encode(const float *interleaved, size_t frames);

    AVCodecContext *m_ctx = nullptr;
    std::vector<AudioCapture *> m_sources;
    qint64 m_clockStart = 0;
    Output m_out;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::vector<float> m_pending; // amostras misturadas ainda sem formar um quadro AAC
    int64_t m_samples = 0;        // amostras já enviadas ao encoder (pts)
};

} // namespace aeonis
