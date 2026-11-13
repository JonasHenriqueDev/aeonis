#pragma once
// Operações sobre arquivos de vídeo feitas dentro do processo com a libavformat/libavcodec,
// no lugar das chamadas ao ffmpeg.exe da versão C#. Todas são bloqueantes: chame fora da UI.

#include <QString>
#include <QStringList>

#include <atomic>

namespace aeonis::Media {

/// Duração em segundos (0 se não der para ler).
double duration(const QString &file);

/// Miniatura JPG com `width` px de largura, tirada a ~2 s (ou do primeiro quadro). Devolve a duração.
/// Miniatura de um quadro do meio do vídeo (ponto sorteado de forma estável pelo nome do arquivo,
/// entre 25% e 60%), pulando quadros escuros (tela de carregamento, transições). Devolve a duração.
double thumbnail(const QString &video, const QString &jpg, int width = 960, int quality = 82);

struct RemuxResult {
    bool ok = false;
    QString error;
    double duration = 0;
    double startOffset = 0; // trim: instante real (s) do primeiro quadro no vídeo original
};

/// Junta segmentos MPEG-TS consecutivos em um MP4 sem re-encode (-c copy, hvc1 para HEVC). O arquivo só
/// aparece com o nome final quando está completo. `faststart` regrava o arquivo inteiro para pôr o índice
/// no começo: vale para clipes, não para gravações longas. `cancel` interrompe (sem gerar o arquivo).
RemuxResult concat(const QStringList &segments, const QString &output, bool faststart = true,
                   const std::atomic<bool> *cancel = nullptr);

/// Limite de envio do Discord: a compressão mira abaixo dele (CompressTargetBytes) e a interface avisa acima.
constexpr qint64 DiscordLimitBytes = 20'000'000;
constexpr qint64 CompressTargetBytes = 19'000'000;

/// O que a compressão precisa saber do vídeo de origem.
struct SourceInfo {
    bool ok = false;
    qint64 bytes = 0;
    double duration = 0;
    int width = 0, height = 0;
    double fps = 0;
    qint64 audioBitrate = 0; // 0 = sem áudio
};
SourceInfo probe(const QString &file);

/// Como comprimir `seconds` de vídeo para caber em CompressTargetBytes: taxa de bits da imagem (x264 com
/// CRF limitado por essa taxa), altura e fps de saída (trechos longos descem de resolução e para 30 fps).
/// `scale` < 1 aperta a meta (nova tentativa quando o arquivo passou). `fits`: cabe em DiscordLimitBytes.
struct CompressPlan {
    int height = 0;
    double fps = 0;
    qint64 videoBitrate = 0;
    qint64 estimatedBytes = 0; // teto: o CRF costuma gerar menos em cenas simples
    bool fits = true;
};
CompressPlan compressPlan(const SourceInfo &source, double seconds, double scale = 1.0);

/// Corta [start, end] (s) sem re-encode. Começa no keyframe anterior a `start` (como o -ss do ffmpeg).
/// `compress`: re-encoda a imagem em H.264 (CompressPlan) mirando menos de 20 MB, começando exatamente em
/// `start` (bem mais lento; o áudio é copiado). Se o arquivo passar do limite, refaz com a meta mais apertada.
RemuxResult trim(const QString &source, const QString &output, double start, double end, bool compress = false);

} // namespace aeonis::Media
