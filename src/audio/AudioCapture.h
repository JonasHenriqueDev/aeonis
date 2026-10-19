#pragma once
// Captura WASAPI em modo compartilhado: loopback do dispositivo de saída padrão (o som do jogo/PC)
// ou um microfone. O Windows converte para float32 estéreo 48 kHz (AUTOCONVERTPCM); as amostras
// ficam numa fila que o AudioMixer consome. O loopback acompanha a troca do dispositivo padrão.

#include <QString>
#include <QStringList>

#include <atomic>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace aeonis {

class AudioCapture {
public:
    enum class Kind { Loopback, Microphone };
    static constexpr int SampleRate = 48000;
    static constexpr int Channels = 2;

    AudioCapture(Kind kind, const QString &deviceHint);
    ~AudioCapture();

    /// Abre o dispositivo e começa a capturar numa thread própria.
    bool start(QString *error);
    void stop();

    /// Quadros (amostras por canal) disponíveis na fila.
    size_t available() const;
    /// Lê até `frames` quadros intercalados para `dst`. Devolve quantos leu.
    size_t read(float *dst, size_t frames);
    void discard(size_t frames);
    /// Instante (QueryPerformanceCounter) do último pacote com som real.
    qint64 lastDataTicks() const { return m_lastData.load(); }
    QString deviceName() const;
    Kind kind() const { return m_kind; }

    /// Nomes dos microfones ativos.
    static QStringList microphones();

private:
    void run(std::promise<bool> *opened);
    void push(const float *data, size_t frames);

    Kind m_kind;
    QString m_hint;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<qint64> m_lastData{0};
    mutable std::mutex m_mutex;
    std::vector<float> m_ring;  // circular, Channels floats por quadro
    size_t m_head = 0, m_count = 0; // em quadros
    QString m_deviceName;
    QString m_error;
};

} // namespace aeonis
