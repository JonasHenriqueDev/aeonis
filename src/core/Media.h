#pragma once
// Operações sobre arquivos de vídeo feitas dentro do processo com a libavformat/libavcodec,
// no lugar das chamadas ao ffmpeg.exe da versão C#. Todas são bloqueantes: chame fora da UI.

#include <QString>
#include <QStringList>

namespace aeonis::Media {

/// Duração em segundos (0 se não der para ler).
double duration(const QString &file);

/// Miniatura JPG com `width` px de largura, tirada a ~2 s (ou do primeiro quadro). Devolve a duração.
/// Miniatura de um quadro do meio do vídeo (ponto sorteado de forma estável pelo nome do arquivo,
/// entre 25% e 60%), pulando quadros escuros (tela de carregamento, transições). Devolve a duração.
double thumbnail(const QString &video, const QString &jpg, int width = 960);

struct RemuxResult {
    bool ok = false;
    QString error;
    double duration = 0;
    double startOffset = 0; // trim: instante real (s) do primeiro quadro no vídeo original
};

/// Junta segmentos MPEG-TS consecutivos em um MP4 sem re-encode (-c copy, faststart, hvc1 para HEVC).
RemuxResult concat(const QStringList &segments, const QString &output);

/// Corta [start, end] (s) sem re-encode. Começa no keyframe anterior a `start` (como o -ss do ffmpeg).
RemuxResult trim(const QString &source, const QString &output, double start, double end);

} // namespace aeonis::Media
