#pragma once
// Encoders de vídeo da libavcodec, na mesma lista e ordem de detecção da versão C#:
// NVENC -> AMF -> Quick Sync -> x264 (CPU). NVENC e AMF recebem a textura D3D11 da captura
// direto (zero-copy); Quick Sync e x264 recebem o quadro copiado para a RAM e convertido.
//
// Keyframe (IDR) forçado a cada 2 s, sem B-frames: cada segmento começa num keyframe e os
// clipes saem por concatenação sem re-encode.

#include "core/Settings.h"

#include <QList>
#include <QString>

#include <functional>
#include <memory>

#include <d3d11.h>

struct AVBufferRef;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace aeonis {

struct EncoderInfo {
    EncoderChoice choice;
    const char *codec;
    QString label;
    bool gpuInput; // recebe AV_PIX_FMT_D3D11
    bool hevc() const;
};

class VideoEncoder {
public:
    using Output = std::function<void(AVPacket *)>;

    static const QList<EncoderInfo> &all();
    static const EncoderInfo &info(EncoderChoice choice);

    /// Abre o encoder escolhido. Em Auto, testa na ordem e lembra o primeiro que funcionar
    /// (por adaptador). `forceFallback`: ignora a lembrança e recomeça a detecção.
    static std::unique_ptr<VideoEncoder> open(EncoderChoice choice, ID3D11Device *device, LUID adapter, int width,
                                              int height, int fps, int bitrateMbps, QString *error);

    ~VideoEncoder();

    /// Codifica a textura (BGRA, tamanho do encoder). `pts` em 1/fps.
    bool encode(ID3D11Texture2D *texture, int64_t pts, bool keyFrame, const Output &out);
    void flush(const Output &out);

    const EncoderInfo &info() const { return m_info; }
    AVCodecContext *context() const { return m_ctx; }

private:
    VideoEncoder() = default;
    bool init(const EncoderInfo &info, ID3D11Device *device, int width, int height, int fps, int bitrateMbps,
              QString *error);
    bool drain(const Output &out);

    EncoderInfo m_info{};
    AVCodecContext *m_ctx = nullptr;
    AVBufferRef *m_deviceRef = nullptr;
    AVBufferRef *m_framesRef = nullptr;
    ID3D11Device *m_device = nullptr;
    ID3D11DeviceContext *m_d3d = nullptr;
    ID3D11Texture2D *m_staging = nullptr;
    AVFrame *m_swFrame = nullptr;
    AVPacket *m_packet = nullptr;
    SwsContext *m_sws = nullptr;
};

} // namespace aeonis
