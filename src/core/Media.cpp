#include "Media.h"
#include "Log.h"

#include <QFile>
#include <QFileInfo>
#include <QImage>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace aeonis::Media {

namespace {

QString avError(int code)
{
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

struct InputDeleter {
    void operator()(AVFormatContext *ctx) const { avformat_close_input(&ctx); }
};
using Input = std::unique_ptr<AVFormatContext, InputDeleter>;

Input openInput(const QString &file, bool probe, QString *error = nullptr)
{
    AVFormatContext *ctx = nullptr;
    const QByteArray path = file.toUtf8();
    int ret = avformat_open_input(&ctx, path.constData(), nullptr, nullptr);
    if (ret < 0) {
        if (error) *error = QStringLiteral("abrir %1: %2").arg(file, avError(ret));
        return {};
    }
    Input in(ctx);
    if (probe) {
        ret = avformat_find_stream_info(ctx, nullptr);
        if (ret < 0) {
            if (error) *error = QStringLiteral("analisar %1: %2").arg(file, avError(ret));
            return {};
        }
    }
    return in;
}

int firstStream(AVFormatContext *ctx, AVMediaType type)
{
    for (unsigned i = 0; i < ctx->nb_streams; ++i)
        if (ctx->streams[i]->codecpar->codec_type == type)
            return int(i);
    return -1;
}

/// MP4 de saída com uma faixa de vídeo e (opcional) uma de áudio, copiadas de `in`.
class Mp4Writer {
public:
    ~Mp4Writer()
    {
        if (m_ctx) {
            if (m_ctx->pb) avio_closep(&m_ctx->pb);
            avformat_free_context(m_ctx);
        }
    }

    /// `videoEnc`: a faixa de vídeo vem desse encoder (re-encode) em vez de copiada de `in`.
    bool open(const QString &path, AVFormatContext *in, int videoIn, int audioIn, QString *error, bool faststart = true,
              const AVCodecContext *videoEnc = nullptr)
    {
        m_path = path;
        const QByteArray file = path.toUtf8();
        int ret = avformat_alloc_output_context2(&m_ctx, nullptr, "mp4", file.constData());
        if (ret < 0 || !m_ctx) {
            *error = QStringLiteral("criar mp4: %1").arg(avError(ret));
            return false;
        }
        for (int idx : {videoIn, audioIn}) {
            if (idx < 0) {
                m_out.push_back(-1);
                continue;
            }
            AVStream *src = in->streams[idx];
            AVStream *dst = avformat_new_stream(m_ctx, nullptr);
            const bool encoded = videoEnc && idx == videoIn;
            if (encoded)
                avcodec_parameters_from_context(dst->codecpar, videoEnc);
            else
                avcodec_parameters_copy(dst->codecpar, src->codecpar);
            dst->codecpar->codec_tag = 0;
            if (dst->codecpar->codec_id == AV_CODEC_ID_HEVC)
                dst->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1'); // reproduz no Windows/QuickTime
            dst->time_base = encoded ? videoEnc->time_base : src->time_base;
            m_out.push_back(dst->index);
        }
        ret = avio_open(&m_ctx->pb, file.constData(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            *error = QStringLiteral("criar %1: %2").arg(path, avError(ret));
            return false;
        }
        AVDictionary *opts = nullptr;
        if (faststart)
            av_dict_set(&opts, "movflags", "+faststart", 0);
        ret = avformat_write_header(m_ctx, &opts);
        av_dict_free(&opts);
        if (ret < 0) {
            *error = QStringLiteral("cabeçalho mp4: %1").arg(avError(ret));
            return false;
        }
        m_headerWritten = true;
        return true;
    }

    /// `slot` 0 = vídeo, 1 = áudio. Timestamps já relativos ao início, em `tb`.
    bool write(int slot, AVPacket *pkt, AVRational tb)
    {
        const int out = m_out[slot];
        if (out < 0)
            return true;
        AVStream *st = m_ctx->streams[out];
        av_packet_rescale_ts(pkt, tb, st->time_base);
        if (pkt->dts != AV_NOPTS_VALUE) {
            const int64_t none = std::numeric_limits<int64_t>::min();
            // Salto absurdo (timestamp corrompido/volta do relógio do TS): descarta o pacote.
            const int64_t jump = av_rescale_q(60, AVRational{1, 1}, st->time_base);
            if (m_lastDts[slot] == none ? pkt->dts > jump : pkt->dts - m_lastDts[slot] > jump)
                return true;
            if (m_lastDts[slot] != none && pkt->dts <= m_lastDts[slot])
                return true; // duplicado ou fora de ordem: descarta
            m_lastDts[slot] = pkt->dts;
        }
        pkt->stream_index = out;
        pkt->pos = -1;
        const int ret = av_interleaved_write_frame(m_ctx, pkt);
        if (ret < 0) {
            m_error = avError(ret);
            return false;
        }
        ++m_packets;
        return true;
    }

    bool finish(QString *error)
    {
        if (!m_headerWritten)
            return false;
        int ret = av_write_trailer(m_ctx);
        avio_closep(&m_ctx->pb);
        if (ret < 0) {
            *error = QStringLiteral("finalizar mp4: %1").arg(avError(ret));
            return false;
        }
        if (m_packets == 0) {
            *error = QStringLiteral("nenhum quadro");
            return false;
        }
        return true;
    }

    QString lastError() const { return m_error; }

private:
    AVFormatContext *m_ctx = nullptr;
    QString m_path;
    std::vector<int> m_out;
    int64_t m_lastDts[2] = {std::numeric_limits<int64_t>::min(), std::numeric_limits<int64_t>::min()};
    bool m_headerWritten = false;
    qint64 m_packets = 0;
    QString m_error;
};

struct PacketDeleter {
    void operator()(AVPacket *p) const { av_packet_free(&p); }
};

struct CodecDeleter {
    void operator()(AVCodecContext *c) const { avcodec_free_context(&c); }
};
using Codec = std::unique_ptr<AVCodecContext, CodecDeleter>;

struct FrameDeleter {
    void operator()(AVFrame *f) const { av_frame_free(&f); }
};

/// Corte com re-encode da imagem (x264, CRF 26, até 1080p) para um arquivo menor; o áudio é copiado.
RemuxResult compressRange(const QString &source, const QString &output, double start, double end)
{
    RemuxResult r;
    Input in = openInput(source, true, &r.error);
    if (!in)
        return r;
    const int v = firstStream(in.get(), AVMEDIA_TYPE_VIDEO);
    const int a = firstStream(in.get(), AVMEDIA_TYPE_AUDIO);
    if (v < 0) {
        r.error = QStringLiteral("vídeo sem faixa de imagem");
        return r;
    }
    AVStream *vst = in->streams[v];
    const AVCodec *decoder = avcodec_find_decoder(vst->codecpar->codec_id);
    const AVCodec *encoder = avcodec_find_encoder_by_name("libx264");
    if (!decoder || !encoder) {
        r.error = QStringLiteral("codec indisponível para comprimir");
        return r;
    }
    Codec dec(avcodec_alloc_context3(decoder));
    avcodec_parameters_to_context(dec.get(), vst->codecpar);
    dec->pkt_timebase = vst->time_base;
    dec->thread_count = 0;
    int ret = avcodec_open2(dec.get(), decoder, nullptr);
    if (ret < 0) {
        r.error = QStringLiteral("abrir decodificador: %1").arg(avError(ret));
        return r;
    }
    const int srcW = vst->codecpar->width, srcH = vst->codecpar->height;
    if (srcW <= 0 || srcH <= 0) {
        r.error = QStringLiteral("tamanho da imagem desconhecido");
        return r;
    }
    const int outH = std::min(srcH, 1080) & ~1;
    const int outW = std::max(2, int(std::lround(double(srcW) * outH / srcH)) & ~1);
    Codec enc(avcodec_alloc_context3(encoder));
    enc->width = outW;
    enc->height = outH;
    enc->pix_fmt = AV_PIX_FMT_YUV420P;
    enc->time_base = vst->time_base;
    const AVRational fps = av_guess_frame_rate(in.get(), vst, nullptr);
    if (fps.num > 0 && fps.den > 0) {
        enc->framerate = fps;
        enc->gop_size = std::max(1, 2 * fps.num / fps.den);
    }
    enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    enc->thread_count = 0;
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "preset", "veryfast", 0);
    av_dict_set(&opts, "crf", "26", 0);
    ret = avcodec_open2(enc.get(), encoder, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        r.error = QStringLiteral("abrir x264: %1").arg(avError(ret));
        return r;
    }

    const int64_t startUs = int64_t(start * AV_TIME_BASE);
    const int64_t endUs = int64_t(end * AV_TIME_BASE);
    const int64_t originUs = in->start_time != AV_NOPTS_VALUE ? in->start_time : 0;
    const int64_t videoBase = av_rescale_q(originUs + startUs, AV_TIME_BASE_Q, vst->time_base);
    av_seek_frame(in.get(), -1, originUs + startUs, AVSEEK_FLAG_BACKWARD);

    bool ok = false;
    {
        Mp4Writer writer;
        if (!writer.open(output, in.get(), v, a, &r.error, true, enc.get())) {
            in.reset();
            QFile::remove(output);
            return r;
        }
        std::unique_ptr<AVPacket, PacketDeleter> pkt(av_packet_alloc()), out(av_packet_alloc());
        std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc()), scaled(av_frame_alloc());
        scaled->format = AV_PIX_FMT_YUV420P;
        scaled->width = outW;
        scaled->height = outH;
        av_frame_get_buffer(scaled.get(), 0);
        SwsContext *sws = nullptr;
        bool videoDone = false, audioDone = a < 0, failed = false;
        qint64 frames = 0;

        auto drainEncoder = [&] {
            while (!failed && avcodec_receive_packet(enc.get(), out.get()) == 0) {
                if (!writer.write(0, out.get(), enc->time_base)) {
                    r.error = QStringLiteral("gravar: %1").arg(writer.lastError());
                    failed = true;
                }
                av_packet_unref(out.get());
            }
        };
        auto takeFrames = [&] {
            while (!failed && avcodec_receive_frame(dec.get(), frame.get()) == 0) {
                const int64_t pts = frame->best_effort_timestamp != AV_NOPTS_VALUE ? frame->best_effort_timestamp : frame->pts;
                const int64_t us = pts == AV_NOPTS_VALUE ? -1 : av_rescale_q(pts, vst->time_base, AV_TIME_BASE_Q) - originUs;
                if (us > endUs)
                    videoDone = true;
                if (us >= startUs && us <= endUs) {
                    sws = sws_getCachedContext(sws, frame->width, frame->height, AVPixelFormat(frame->format), outW, outH,
                                               AV_PIX_FMT_YUV420P, SWS_BICUBIC, nullptr, nullptr, nullptr);
                    if (sws && av_frame_make_writable(scaled.get()) >= 0) {
                        sws_scale(sws, frame->data, frame->linesize, 0, frame->height, scaled->data, scaled->linesize);
                        scaled->pts = pts - videoBase;
                        scaled->pict_type = AV_PICTURE_TYPE_NONE;
                        if (avcodec_send_frame(enc.get(), scaled.get()) >= 0)
                            ++frames;
                        drainEncoder();
                    }
                }
                av_frame_unref(frame.get());
            }
        };

        while (!failed && !(videoDone && audioDone) && av_read_frame(in.get(), pkt.get()) >= 0) {
            if (pkt->stream_index == v && !videoDone) {
                avcodec_send_packet(dec.get(), pkt.get());
                takeFrames();
            } else if (pkt->stream_index == a && !audioDone) {
                if (pkt->dts == AV_NOPTS_VALUE) pkt->dts = pkt->pts;
                if (pkt->pts == AV_NOPTS_VALUE) pkt->pts = pkt->dts;
                const AVRational tb = in->streams[a]->time_base;
                const int64_t us = pkt->pts == AV_NOPTS_VALUE ? -1 : av_rescale_q(pkt->pts, tb, AV_TIME_BASE_Q) - originUs;
                if (us > endUs) {
                    audioDone = true;
                } else if (us >= startUs) {
                    const int64_t base = av_rescale_q(originUs + startUs, AV_TIME_BASE_Q, tb);
                    pkt->pts -= base;
                    pkt->dts -= base;
                    if (!writer.write(1, pkt.get(), tb)) {
                        r.error = QStringLiteral("gravar: %1").arg(writer.lastError());
                        failed = true;
                    }
                }
            }
            av_packet_unref(pkt.get());
        }
        if (!failed && !videoDone) {
            avcodec_send_packet(dec.get(), nullptr);
            takeFrames();
        }
        if (!failed) {
            avcodec_send_frame(enc.get(), nullptr);
            drainEncoder();
        }
        sws_freeContext(sws);
        if (!failed && frames == 0)
            r.error = QStringLiteral("nenhum quadro no trecho");
        ok = !failed && frames > 0 && writer.finish(&r.error);
        r.startOffset = start;
    }
    if (!ok) {
        QFile::remove(output);
        return r;
    }
    r.ok = true;
    r.duration = duration(output);
    return r;
}

} // namespace

double duration(const QString &file)
{
    Input in = openInput(file, true);
    if (!in || in->duration <= 0)
        return 0;
    return in->duration / double(AV_TIME_BASE);
}

double thumbnail(const QString &video, const QString &jpg, int width, int quality)
{
    Input in = openInput(video, true);
    if (!in)
        return 0;
    const double dur = in->duration > 0 ? in->duration / double(AV_TIME_BASE) : 0;
    const int vs = av_find_best_stream(in.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vs < 0)
        return dur;
    AVStream *st = in->streams[vs];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec)
        return dur;
    AVCodecContext *dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, st->codecpar);
    dec->thread_count = 2;
    if (avcodec_open2(dec, codec, nullptr) < 0) {
        avcodec_free_context(&dec);
        return dur;
    }
    std::unique_ptr<AVPacket, PacketDeleter> pkt(av_packet_alloc());
    AVFrame *frame = av_frame_alloc();
    bool got = false;
    // Brilho médio do quadro (plano Y do YUV; outros formatos contam como claros).
    auto brightness = [](const AVFrame *f) {
        if (f->format != AV_PIX_FMT_YUV420P && f->format != AV_PIX_FMT_NV12 && f->format != AV_PIX_FMT_YUVJ420P)
            return 255.0;
        qint64 sum = 0, n = 0;
        for (int y = 0; y < f->height; y += 8)
            for (int x = 0; x < f->width; x += 8, ++n)
                sum += f->data[0][qint64(y) * f->linesize[0] + x];
        return n ? double(sum) / n : 255.0;
    };
    // Pontos a tentar: um sorteado (estável pelo arquivo) no meio do vídeo e outros de reserva.
    const uint seed = qHash(QFileInfo(video).fileName());
    const double first = 0.25 + (seed % 36) / 100.0;
    const double points[] = {first, 0.5, 0.75, 0.35, 0.15, 0.0};
    for (double point : points) {
        const double at = dur > 2.5 ? dur * point : 0;
        av_seek_frame(in.get(), -1, qint64(at * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(dec);
        bool decoded = false;
        int reads = 0;
        while (!decoded && reads++ < 600 && av_read_frame(in.get(), pkt.get()) >= 0) {
            if (pkt->stream_index == vs && avcodec_send_packet(dec, pkt.get()) >= 0)
                decoded = avcodec_receive_frame(dec, frame) == 0;
            av_packet_unref(pkt.get());
        }
        if (!decoded && avcodec_send_packet(dec, nullptr) >= 0)
            decoded = avcodec_receive_frame(dec, frame) == 0;
        if (!decoded)
            continue;
        got = true;
        if (brightness(frame) > 28 || point == 0.0 || dur <= 2.5)
            break; // claro o bastante (senão tenta o próximo ponto; o último quadro lido fica de reserva)
    }
    if (got && frame->width > 0 && frame->height > 0) {
        const int w = std::min(width, frame->width) & ~1;
        const int h = std::max(2, int(double(frame->height) * w / frame->width) & ~1);
        SwsContext *sws = sws_getContext(frame->width, frame->height, AVPixelFormat(frame->format), w, h,
                                         AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (sws) {
            QImage img(w, h, QImage::Format_RGB888);
            uint8_t *dst[4] = {img.bits(), nullptr, nullptr, nullptr};
            int stride[4] = {int(img.bytesPerLine()), 0, 0, 0};
            sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, stride);
            sws_freeContext(sws);
            img.save(jpg, "JPG", quality);
        }
    }
    av_frame_free(&frame);
    avcodec_free_context(&dec);
    return dur;
}

RemuxResult concat(const QStringList &segments, const QString &output, bool faststart, const std::atomic<bool> *cancel)
{
    RemuxResult r;
    if (segments.isEmpty()) {
        r.error = QStringLiteral("nenhum segmento");
        return r;
    }
    Input first = openInput(segments.first(), true, &r.error);
    if (!first)
        return r;
    const int v0 = firstStream(first.get(), AVMEDIA_TYPE_VIDEO);
    const int a0 = firstStream(first.get(), AVMEDIA_TYPE_AUDIO);
    if (v0 < 0) {
        r.error = QStringLiteral("segmento sem vídeo");
        return r;
    }
    // Escreve em "<saída>.part" e só renomeia no fim: uma interrupção (desligar, fechar à força) nunca
    // deixa um .mp4 pela metade, que não abre, na pasta de vídeos.
    const QString partial = output + QStringLiteral(".part");
    bool writerOk = false;
    {
        Mp4Writer writer;
        if (!writer.open(partial, first.get(), v0, a0, &r.error, faststart)) {
            first.reset();
            QFile::remove(partial);
            return r;
        }
        std::unique_ptr<AVPacket, PacketDeleter> pkt(av_packet_alloc());
        int64_t baseUs = AV_NOPTS_VALUE;
        bool failed = false;
        for (int i = 0; i < segments.size() && !failed; ++i) {
            if (cancel && cancel->load()) {
                r.error = QStringLiteral("cancelado");
                failed = true;
                break;
            }
            Input in = i == 0 ? std::move(first) : openInput(segments[i], false);
            if (!in)
                continue; // segmento ilegível (ex.: apagado): segue com os outros
            const int v = firstStream(in.get(), AVMEDIA_TYPE_VIDEO);
            const int a = a0 >= 0 ? firstStream(in.get(), AVMEDIA_TYPE_AUDIO) : -1;
            while (av_read_frame(in.get(), pkt.get()) >= 0) {
                const int slot = pkt->stream_index == v ? 0 : (pkt->stream_index == a && a >= 0) ? 1 : -1;
                if (slot < 0 || (pkt->dts == AV_NOPTS_VALUE && pkt->pts == AV_NOPTS_VALUE)) {
                    av_packet_unref(pkt.get());
                    continue;
                }
                if (pkt->dts == AV_NOPTS_VALUE) pkt->dts = pkt->pts;
                if (pkt->pts == AV_NOPTS_VALUE) pkt->pts = pkt->dts;
                const AVRational tb = in->streams[pkt->stream_index]->time_base;
                const int64_t dtsUs = av_rescale_q(pkt->dts, tb, AV_TIME_BASE_Q);
                if (baseUs == AV_NOPTS_VALUE) {
                    // O clipe começa no primeiro keyframe de vídeo; áudio anterior é descartado.
                    if (slot != 0 || !(pkt->flags & AV_PKT_FLAG_KEY)) {
                        av_packet_unref(pkt.get());
                        continue;
                    }
                    baseUs = dtsUs;
                }
                const int64_t base = av_rescale_q(baseUs, AV_TIME_BASE_Q, tb);
                pkt->pts -= base;
                pkt->dts -= base;
                if (pkt->dts < 0) {
                    av_packet_unref(pkt.get());
                    continue;
                }
                if (!writer.write(slot, pkt.get(), tb)) {
                    r.error = QStringLiteral("gravar: %1").arg(writer.lastError());
                    failed = true;
                    av_packet_unref(pkt.get());
                    break;
                }
                av_packet_unref(pkt.get());
            }
        }
        writerOk = !failed && writer.finish(&r.error);
    }
    if (!writerOk || QFileInfo(partial).size() == 0) {
        QFile::remove(partial);
        if (r.error.isEmpty()) r.error = QStringLiteral("arquivo vazio");
        return r;
    }
    QFile::remove(output);
    if (!QFile::rename(partial, output)) {
        QFile::remove(partial);
        r.error = QStringLiteral("renomear para %1").arg(output);
        return r;
    }
    r.ok = true;
    r.duration = duration(output);
    return r;
}

RemuxResult trim(const QString &source, const QString &output, double start, double end, bool compress)
{
    if (compress)
        return compressRange(source, output, start, end);
    RemuxResult r;
    Input in = openInput(source, true, &r.error);
    if (!in)
        return r;
    const int v = firstStream(in.get(), AVMEDIA_TYPE_VIDEO);
    const int a = firstStream(in.get(), AVMEDIA_TYPE_AUDIO);
    if (v < 0) {
        r.error = QStringLiteral("vídeo sem faixa de imagem");
        return r;
    }
    const int64_t startUs = int64_t(start * AV_TIME_BASE);
    const int64_t endUs = int64_t(end * AV_TIME_BASE);
    const int64_t originUs = in->start_time != AV_NOPTS_VALUE ? in->start_time : 0;
    av_seek_frame(in.get(), -1, originUs + startUs, AVSEEK_FLAG_BACKWARD);

    bool ok = false;
    {
        Mp4Writer writer;
        if (!writer.open(output, in.get(), v, a, &r.error)) {
            in.reset();
            QFile::remove(output);
            return r;
        }
        std::unique_ptr<AVPacket, PacketDeleter> pkt(av_packet_alloc());
        int64_t baseUs = AV_NOPTS_VALUE;
        bool done[2] = {false, a < 0};
        bool failed = false;
        while (!(done[0] && done[1]) && av_read_frame(in.get(), pkt.get()) >= 0) {
            const int slot = pkt->stream_index == v ? 0 : pkt->stream_index == a ? 1 : -1;
            if (slot < 0 || done[slot] || (pkt->dts == AV_NOPTS_VALUE && pkt->pts == AV_NOPTS_VALUE)) {
                av_packet_unref(pkt.get());
                continue;
            }
            if (pkt->dts == AV_NOPTS_VALUE) pkt->dts = pkt->pts;
            if (pkt->pts == AV_NOPTS_VALUE) pkt->pts = pkt->dts;
            const AVRational tb = in->streams[pkt->stream_index]->time_base;
            const int64_t ptsUs = av_rescale_q(pkt->pts, tb, AV_TIME_BASE_Q) - originUs;
            const int64_t dtsUs = av_rescale_q(pkt->dts, tb, AV_TIME_BASE_Q) - originUs;
            if (ptsUs > endUs) {
                done[slot] = true;
                av_packet_unref(pkt.get());
                continue;
            }
            if (baseUs == AV_NOPTS_VALUE) {
                if (slot != 0 || !(pkt->flags & AV_PKT_FLAG_KEY)) {
                    av_packet_unref(pkt.get());
                    continue;
                }
                baseUs = dtsUs;
            }
            const int64_t base = av_rescale_q(baseUs + originUs, AV_TIME_BASE_Q, tb);
            pkt->pts -= base;
            pkt->dts -= base;
            if (pkt->dts < 0) {
                av_packet_unref(pkt.get());
                continue;
            }
            if (!writer.write(slot, pkt.get(), tb)) {
                r.error = QStringLiteral("gravar: %1").arg(writer.lastError());
                failed = true;
                av_packet_unref(pkt.get());
                break;
            }
            av_packet_unref(pkt.get());
        }
        ok = !failed && writer.finish(&r.error);
        if (baseUs != AV_NOPTS_VALUE)
            r.startOffset = baseUs / double(AV_TIME_BASE);
    }
    if (!ok) {
        QFile::remove(output);
        return r;
    }
    r.ok = true;
    r.duration = duration(output);
    return r;
}

} // namespace aeonis::Media
