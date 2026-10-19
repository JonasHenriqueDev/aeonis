#include "SoundEffects.h"

#include <QByteArray>
#include <QtMath>

#include <windows.h>
#include <mmsystem.h>

#include <cstring>

namespace aeonis::SoundEffects {

namespace {

/// WAV PCM 16 bits mono: duas notas ascendentes (dó-sol) com envelope suave.
QByteArray chime()
{
    constexpr int rate = 44100;
    const struct { double freq; double start; double length; } notes[] = {{1046.5, 0.0, 0.11}, {1568.0, 0.07, 0.20}};
    const int total = int(rate * 0.30);
    QByteArray pcm(total * 2, '\0');
    auto *samples = reinterpret_cast<qint16 *>(pcm.data());
    for (int i = 0; i < total; ++i) {
        const double t = double(i) / rate;
        double v = 0;
        for (const auto &n : notes) {
            const double local = t - n.start;
            if (local < 0 || local > n.length)
                continue;
            const double attack = std::min(1.0, local / 0.006);
            const double release = std::exp(-local * 18.0);
            v += std::sin(2 * M_PI * n.freq * local) * attack * release * 0.22;
        }
        samples[i] = qint16(std::clamp(v, -1.0, 1.0) * 32767);
    }
    QByteArray wav;
    auto u32 = [&wav](quint32 x) { wav.append(reinterpret_cast<const char *>(&x), 4); };
    auto u16 = [&wav](quint16 x) { wav.append(reinterpret_cast<const char *>(&x), 2); };
    wav.append("RIFF");
    u32(36 + quint32(pcm.size()));
    wav.append("WAVEfmt ");
    u32(16);
    u16(1);           // PCM
    u16(1);           // mono
    u32(rate);
    u32(rate * 2);    // bytes/s
    u16(2);           // block align
    u16(16);          // bits
    wav.append("data");
    u32(quint32(pcm.size()));
    wav.append(pcm);
    return wav;
}

} // namespace

void clipSaved()
{
    static const QByteArray sound = chime(); // precisa viver enquanto toca (SND_ASYNC)
    PlaySoundW(reinterpret_cast<LPCWSTR>(sound.constData()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

} // namespace aeonis::SoundEffects
