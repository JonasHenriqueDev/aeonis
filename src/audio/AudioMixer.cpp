#include "AudioMixer.h"
#include "core/Log.h"

#include <windows.h>

#include <algorithm>
#include <deque>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
}

namespace aeonis {

namespace {

constexpr int Rate = AudioCapture::SampleRate;
constexpr int Ch = AudioCapture::Channels;

qint64 qpcNow()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

qint64 qpcFrequency()
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
}

} // namespace

AudioMixer::AudioMixer() = default;

AudioMixer::~AudioMixer()
{
    stop();
    if (m_ctx)
        avcodec_free_context(&m_ctx);
}

bool AudioMixer::open(QString *error)
{
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) {
        *error = QStringLiteral("encoder AAC indisponível");
        return false;
    }
    m_ctx = avcodec_alloc_context3(codec);
    m_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    m_ctx->sample_rate = Rate;
    av_channel_layout_default(&m_ctx->ch_layout, Ch);
    m_ctx->bit_rate = 160'000;
    m_ctx->time_base = {1, Rate};
    const int ret = avcodec_open2(m_ctx, codec, nullptr);
    if (ret < 0) {
        *error = QStringLiteral("AAC: %1").arg(ret);
        return false;
    }
    return true;
}

void AudioMixer::start(std::vector<AudioCapture *> sources, qint64 clockStart, Output out)
{
    stop();
    m_sources = std::move(sources);
    m_clockStart = clockStart;
    m_out = std::move(out);
    m_pending.clear();
    m_samples = 0;
    m_running = true;
    m_thread = std::thread(&AudioMixer::run, this);
}

void AudioMixer::stop()
{
    if (!m_thread.joinable())
        return;
    m_running = false;
    m_thread.join();
    if (m_ctx && avcodec_send_frame(m_ctx, nullptr) >= 0) {
        AVPacket *pkt = av_packet_alloc();
        while (avcodec_receive_packet(m_ctx, pkt) == 0) {
            if (m_out) m_out(pkt);
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);
    }
}

void AudioMixer::run()
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    const qint64 freq = qpcFrequency();
    const qint64 silenceTicks = freq * 80 / 1000;
    const size_t maxBacklog = size_t(Rate * 0.30);
    const size_t keepBacklog = size_t(Rate * 0.06);
    const int64_t farBehind = int64_t(Rate * 0.12);

    const size_t n = m_sources.size();
    std::vector<int64_t> written(n, 0);            // quadros já alinhados por fonte
    std::vector<std::deque<float>> aligned(n);     // quadros alinhados ao relógio, ainda não misturados
    std::vector<float> chunk;
    std::vector<float> mix;

    while (m_running) {
        Sleep(10);
        const qint64 now = qpcNow();
        const int64_t target = (now - m_clockStart) * Rate / freq;
        for (size_t i = 0; i < n; ++i) {
            AudioCapture *src = m_sources[i];
            int64_t due = target - written[i];
            if (due <= 0)
                continue;
            // Se a captura acumulou atraso, descarta o excesso.
            const size_t backlog = src->available();
            if (backlog > maxBacklog)
                src->discard(backlog - keepBacklog);
            chunk.resize(size_t(due) * Ch);
            const size_t got = src->read(chunk.data(), size_t(due));
            aligned[i].insert(aligned[i].end(), chunk.begin(), chunk.begin() + got * Ch);
            written[i] += int64_t(got);
            due -= int64_t(got);
            const bool silent = now - src->lastDataTicks() > silenceTicks;
            if (due > 0 && (silent || due > farBehind)) {
                aligned[i].insert(aligned[i].end(), size_t(due) * Ch, 0.f);
                written[i] += due;
            }
        }
        size_t ready = SIZE_MAX;
        for (size_t i = 0; i < n; ++i)
            ready = std::min(ready, aligned[i].size() / Ch);
        if (n == 0 || ready == 0 || ready == SIZE_MAX)
            continue;
        mix.assign(ready * Ch, 0.f);
        for (size_t i = 0; i < n; ++i) {
            for (size_t s = 0; s < ready * Ch; ++s)
                mix[s] += aligned[i][s];
            aligned[i].erase(aligned[i].begin(), aligned[i].begin() + ready * Ch);
        }
        for (float &v : mix)
            v = std::clamp(v, -1.f, 1.f);
        encode(mix.data(), ready);
    }
}

void AudioMixer::encode(const float *interleaved, size_t frames)
{
    m_pending.insert(m_pending.end(), interleaved, interleaved + frames * Ch);
    const int frameSize = m_ctx->frame_size > 0 ? m_ctx->frame_size : 1024;
    AVPacket *pkt = av_packet_alloc();
    while (m_pending.size() >= size_t(frameSize) * Ch) {
        AVFrame *frame = av_frame_alloc();
        frame->format = AV_SAMPLE_FMT_FLTP;
        frame->sample_rate = Rate;
        av_channel_layout_copy(&frame->ch_layout, &m_ctx->ch_layout);
        frame->nb_samples = frameSize;
        av_frame_get_buffer(frame, 0);
        auto *left = reinterpret_cast<float *>(frame->data[0]);
        auto *right = reinterpret_cast<float *>(frame->data[1]);
        for (int s = 0; s < frameSize; ++s) {
            left[s] = m_pending[size_t(s) * Ch];
            right[s] = m_pending[size_t(s) * Ch + 1];
        }
        m_pending.erase(m_pending.begin(), m_pending.begin() + size_t(frameSize) * Ch);
        frame->pts = m_samples;
        m_samples += frameSize;
        if (avcodec_send_frame(m_ctx, frame) >= 0) {
            while (avcodec_receive_packet(m_ctx, pkt) == 0) {
                if (m_out) m_out(pkt);
                av_packet_unref(pkt);
            }
        }
        av_frame_free(&frame);
    }
    av_packet_free(&pkt);
}

} // namespace aeonis
