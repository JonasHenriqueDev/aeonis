#include "SegmentWriter.h"
#include "core/Log.h"

#include <QDir>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace aeonis {

namespace {

QString avError(int code)
{
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

} // namespace

SegmentWriter::~SegmentWriter() { close(); }

bool SegmentWriter::open(const QString &dir, int firstIndex, AVCodecContext *video, AVCodecContext *audio, QString *error)
{
    std::lock_guard lock(m_mutex);
    m_dir = dir;
    m_nextIndex = firstIndex;
    m_index = -1;
    m_video = video;
    m_audio = audio;
    m_started = false;
    m_bytes = 0;
    // O primeiro arquivo só abre com o primeiro keyframe; valida já que o muxer aceita os parâmetros.
    AVFormatContext *probe = nullptr;
    if (avformat_alloc_output_context2(&probe, nullptr, "mpegts", nullptr) < 0 || !probe) {
        *error = QStringLiteral("muxer MPEG-TS indisponível");
        return false;
    }
    avformat_free_context(probe);
    return true;
}

bool SegmentWriter::startSegment(QString *error)
{
    const QString path = QDir::toNativeSeparators(m_dir + QStringLiteral("/seg_%1.ts").arg(m_nextIndex, 6, 10, QLatin1Char('0')));
    const QByteArray file = path.toUtf8();
    int ret = avformat_alloc_output_context2(&m_fmt, nullptr, "mpegts", file.constData());
    if (ret < 0 || !m_fmt) {
        *error = avError(ret);
        return false;
    }
    for (AVCodecContext *ctx : {m_video, m_audio}) {
        if (!ctx)
            continue;
        AVStream *st = avformat_new_stream(m_fmt, nullptr);
        avcodec_parameters_from_context(st->codecpar, ctx);
        st->time_base = ctx->time_base;
    }
    ret = avio_open(&m_fmt->pb, file.constData(), AVIO_FLAG_WRITE);
    if (ret >= 0)
        ret = avformat_write_header(m_fmt, nullptr);
    if (ret < 0) {
        *error = QStringLiteral("%1: %2").arg(path, avError(ret));
        if (m_fmt->pb)
            avio_closep(&m_fmt->pb);
        avformat_free_context(m_fmt);
        m_fmt = nullptr;
        return false;
    }
    m_index = m_nextIndex++;
    return true;
}

void SegmentWriter::finishSegment()
{
    if (!m_fmt)
        return;
    av_write_trailer(m_fmt);
    m_bytes += avio_size(m_fmt->pb) > 0 ? avio_size(m_fmt->pb) : 0;
    avio_closep(&m_fmt->pb);
    avformat_free_context(m_fmt);
    m_fmt = nullptr;
}

void SegmentWriter::writeVideo(AVPacket *pkt)
{
    std::lock_guard lock(m_mutex);
    if (!m_video)
        return;
    const bool key = pkt->flags & AV_PKT_FLAG_KEY;
    if (!m_started && !key)
        return; // cada segmento começa num keyframe
    const AVRational tb = m_video->time_base;
    const int64_t segmentLength = av_rescale_q(SegmentSeconds, AVRational{1, 1}, tb);
    // Troca de arquivo no keyframe que vem depois de ~2 s (meio quadro de folga).
    const AVRational frame = m_video->framerate.num > 0 ? av_inv_q(m_video->framerate) : AVRational{1, 60};
    const int64_t slack = av_rescale_q(1, frame, tb) / 2;
    if (!m_started || (key && pkt->pts - m_segmentStart >= segmentLength - slack)) {
        finishSegment();
        QString err;
        if (!startSegment(&err)) {
            Log::error(QStringLiteral("Segmento: %1").arg(err));
            return;
        }
        m_started = true;
        m_segmentStart = pkt->pts;
    }
    if (!m_fmt)
        return;
    AVPacket *copy = av_packet_clone(pkt);
    copy->stream_index = 0;
    av_packet_rescale_ts(copy, tb, m_fmt->streams[0]->time_base);
    const int ret = av_write_frame(m_fmt, copy);
    av_packet_free(&copy);
    if (ret < 0)
        Log::warn(QStringLiteral("Segmento %1: vídeo: %2").arg(m_index).arg(avError(ret)));
    avio_flush(m_fmt->pb); // clipes leem o segmento em andamento
}

void SegmentWriter::writeAudio(AVPacket *pkt)
{
    std::lock_guard lock(m_mutex);
    if (!m_audio || !m_started || !m_fmt || m_fmt->nb_streams < 2)
        return;
    // O AAC começa com timestamps negativos (atraso do encoder): no MPEG-TS eles dariam a volta
    // no relógio de 33 bits. O vídeo começa em >= 0, então esse áudio inicial não faz falta.
    if (pkt->pts != AV_NOPTS_VALUE && pkt->pts < 0)
        return;
    AVPacket *copy = av_packet_clone(pkt);
    copy->stream_index = 1;
    av_packet_rescale_ts(copy, m_audio->time_base, m_fmt->streams[1]->time_base);
    const int ret = av_write_frame(m_fmt, copy);
    av_packet_free(&copy);
    if (ret < 0)
        Log::warn(QStringLiteral("Segmento %1: áudio: %2").arg(m_index).arg(avError(ret)));
}

void SegmentWriter::close()
{
    std::lock_guard lock(m_mutex);
    finishSegment();
    m_started = false;
    m_video = nullptr;
    m_audio = nullptr;
}

int SegmentWriter::currentIndex() const
{
    std::lock_guard lock(m_mutex);
    return m_index;
}

qint64 SegmentWriter::bytesWritten() const
{
    std::lock_guard lock(m_mutex);
    return m_bytes;
}

} // namespace aeonis
