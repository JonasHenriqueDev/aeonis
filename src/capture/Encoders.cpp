#include "Encoders.h"
#include "core/Log.h"

#include <map>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace aeonis {

namespace {

QString avError(int code)
{
    char buf[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

std::mutex detectGate;
std::map<qint64, EncoderChoice> detected; // por adaptador (LUID)

qint64 luidKey(LUID l) { return (qint64(l.HighPart) << 32) | l.LowPart; }

} // namespace

bool EncoderInfo::hevc() const { return QByteArray(codec).startsWith("hevc"); }

const QList<EncoderInfo> &VideoEncoder::all()
{
    static const QList<EncoderInfo> list = {
        {EncoderChoice::NvidiaH264, "h264_nvenc", QStringLiteral("NVIDIA NVENC H.264"), true},
        {EncoderChoice::NvidiaHevc, "hevc_nvenc", QStringLiteral("NVIDIA NVENC HEVC"), true},
        {EncoderChoice::AmdH264, "h264_amf", QStringLiteral("AMD AMF H.264"), true},
        {EncoderChoice::IntelH264, "h264_qsv", QStringLiteral("Intel Quick Sync H.264"), false},
        {EncoderChoice::CpuX264, "libx264", QStringLiteral("CPU x264 (pesado)"), false},
    };
    return list;
}

const EncoderInfo &VideoEncoder::info(EncoderChoice choice)
{
    for (const EncoderInfo &e : all())
        if (e.choice == choice)
            return e;
    return all().last();
}

std::unique_ptr<VideoEncoder> VideoEncoder::open(EncoderChoice choice, ID3D11Device *device, LUID adapter, int width,
                                                 int height, int fps, int bitrateMbps, QString *error)
{
    auto tryOpen = [&](const EncoderInfo &info, bool probe) -> std::unique_ptr<VideoEncoder> {
        if (probe) {
            // Testa com um quadro de verdade num encoder descartável: abrir nem sempre basta.
            std::unique_ptr<VideoEncoder> test(new VideoEncoder);
            QString err;
            if (!test->init(info, device, width, height, fps, bitrateMbps, &err))
                return nullptr;
            D3D11_TEXTURE2D_DESC d{};
            d.Width = UINT(width);
            d.Height = UINT(height);
            d.MipLevels = 1;
            d.ArraySize = 1;
            d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_RENDER_TARGET;
            ID3D11Texture2D *tex = nullptr;
            if (FAILED(device->CreateTexture2D(&d, nullptr, &tex)))
                return nullptr;
            int packets = 0;
            const Output count = [&packets](AVPacket *) { ++packets; };
            bool ok = test->encode(tex, 0, true, count);
            if (ok)
                test->flush(count);
            tex->Release();
            if (!ok || packets == 0)
                return nullptr;
        }
        std::unique_ptr<VideoEncoder> enc(new VideoEncoder);
        QString err;
        if (!enc->init(info, device, width, height, fps, bitrateMbps, &err)) {
            if (!probe)
                Log::warn(QStringLiteral("Encoder %1 indisponível: %2").arg(info.label, err));
            return nullptr;
        }
        return enc;
    };

    if (choice != EncoderChoice::Auto) {
        if (auto enc = tryOpen(info(choice), false))
            return enc;
        Log::warn(QStringLiteral("%1 não abriu; usando a detecção automática.").arg(info(choice).label));
    }

    std::lock_guard lock(detectGate);
    const qint64 key = luidKey(adapter);
    if (auto it = detected.find(key); it != detected.end())
        if (auto enc = tryOpen(info(it->second), false))
            return enc;
    for (const EncoderInfo &e : all()) {
        if (e.choice == EncoderChoice::NvidiaHevc)
            continue;
        const bool last = e.choice == EncoderChoice::CpuX264;
        if (auto enc = tryOpen(e, !last)) {
            if (last)
                Log::warn(QStringLiteral("Nenhum encoder de hardware disponível; usando x264 na CPU."));
            else
                Log::info(QStringLiteral("Encoder detectado: %1").arg(e.label));
            detected[key] = e.choice;
            return enc;
        }
    }
    *error = QStringLiteral("nenhum encoder de vídeo disponível");
    return nullptr;
}

VideoEncoder::~VideoEncoder()
{
    if (m_ctx)
        avcodec_free_context(&m_ctx);
    av_buffer_unref(&m_framesRef);
    av_buffer_unref(&m_deviceRef);
    if (m_swFrame)
        av_frame_free(&m_swFrame);
    if (m_packet)
        av_packet_free(&m_packet);
    if (m_sws)
        sws_freeContext(m_sws);
    if (m_staging)
        m_staging->Release();
    if (m_d3d)
        m_d3d->Release();
}

bool VideoEncoder::init(const EncoderInfo &info, ID3D11Device *device, int width, int height, int fps, int bitrateMbps,
                        QString *error)
{
    m_info = info;
    m_device = device;
    device->GetImmediateContext(&m_d3d);
    const AVCodec *codec = avcodec_find_encoder_by_name(info.codec);
    if (!codec) {
        *error = QStringLiteral("não incluído nesta compilação");
        return false;
    }
    m_ctx = avcodec_alloc_context3(codec);
    m_ctx->width = width;
    m_ctx->height = height;
    m_ctx->time_base = {1, fps};
    m_ctx->framerate = {fps, 1};
    m_ctx->gop_size = fps * 2;
    m_ctx->max_b_frames = 0;
    const int64_t bps = int64_t(bitrateMbps) * 1'000'000;
    m_ctx->bit_rate = bps;
    m_ctx->rc_max_rate = bps;
    m_ctx->rc_buffer_size = int(std::min<int64_t>(bps * 2, INT_MAX));

    if (info.gpuInput) {
        m_deviceRef = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
        auto *dev = reinterpret_cast<AVHWDeviceContext *>(m_deviceRef->data);
        auto *d3d = static_cast<AVD3D11VADeviceContext *>(dev->hwctx);
        device->AddRef(); // a libavutil libera ao destruir o contexto
        d3d->device = device;
        int ret = av_hwdevice_ctx_init(m_deviceRef);
        if (ret < 0) {
            *error = QStringLiteral("contexto D3D11: %1").arg(avError(ret));
            return false;
        }
        m_framesRef = av_hwframe_ctx_alloc(m_deviceRef);
        auto *frames = reinterpret_cast<AVHWFramesContext *>(m_framesRef->data);
        frames->format = AV_PIX_FMT_D3D11;
        frames->sw_format = AV_PIX_FMT_BGRA;
        frames->width = width;
        frames->height = height;
        static_cast<AVD3D11VAFramesContext *>(frames->hwctx)->BindFlags = D3D11_BIND_RENDER_TARGET;
        ret = av_hwframe_ctx_init(m_framesRef);
        if (ret < 0) {
            *error = QStringLiteral("quadros D3D11: %1").arg(avError(ret));
            return false;
        }
        m_ctx->pix_fmt = AV_PIX_FMT_D3D11;
        m_ctx->sw_pix_fmt = AV_PIX_FMT_BGRA;
        m_ctx->hw_frames_ctx = av_buffer_ref(m_framesRef);
        m_ctx->hw_device_ctx = av_buffer_ref(m_deviceRef);
    } else {
        const AVPixelFormat fmt = info.choice == EncoderChoice::IntelH264 ? AV_PIX_FMT_NV12 : AV_PIX_FMT_YUV420P;
        m_ctx->pix_fmt = fmt;
        m_ctx->colorspace = AVCOL_SPC_BT709;
        m_ctx->color_primaries = AVCOL_PRI_BT709;
        m_ctx->color_trc = AVCOL_TRC_BT709;
        m_ctx->color_range = AVCOL_RANGE_MPEG;
        D3D11_TEXTURE2D_DESC d{};
        d.Width = UINT(width);
        d.Height = UINT(height);
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device->CreateTexture2D(&d, nullptr, &m_staging))) {
            *error = QStringLiteral("textura de leitura");
            return false;
        }
        m_swFrame = av_frame_alloc();
        m_swFrame->format = fmt;
        m_swFrame->width = width;
        m_swFrame->height = height;
        av_frame_get_buffer(m_swFrame, 0);
        m_sws = sws_getContext(width, height, AV_PIX_FMT_BGRA, width, height, fmt, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!m_sws) {
            *error = QStringLiteral("conversão de cores");
            return false;
        }
        const int *bt709 = sws_getCoefficients(SWS_CS_ITU709);
        sws_setColorspaceDetails(m_sws, bt709, 1, bt709, 0, 0, 1 << 16, 1 << 16);
    }

    AVDictionary *opts = nullptr;
    switch (info.choice) {
    case EncoderChoice::NvidiaH264:
    case EncoderChoice::NvidiaHevc:
        av_dict_set(&opts, "preset", "p4", 0);
        av_dict_set(&opts, "tune", "hq", 0);
        av_dict_set(&opts, "rc", "cbr", 0);
        av_dict_set(&opts, "forced-idr", "1", 0);
        break;
    case EncoderChoice::AmdH264:
        av_dict_set(&opts, "usage", "transcoding", 0);
        av_dict_set(&opts, "quality", "speed", 0);
        av_dict_set(&opts, "rc", "cbr", 0);
        av_dict_set(&opts, "forced_idr", "1", 0);
        break;
    case EncoderChoice::IntelH264:
        av_dict_set(&opts, "preset", "veryfast", 0);
        av_dict_set(&opts, "forced_idr", "1", 0);
        break;
    default:
        av_dict_set(&opts, "preset", "veryfast", 0);
        av_dict_set(&opts, "forced-idr", "1", 0);
        break;
    }
    const int ret = avcodec_open2(m_ctx, codec, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        *error = avError(ret);
        return false;
    }
    m_packet = av_packet_alloc();
    return true;
}

bool VideoEncoder::encode(ID3D11Texture2D *texture, int64_t pts, bool keyFrame, const Output &out)
{
    AVFrame *frame = nullptr;
    if (m_info.gpuInput) {
        frame = av_frame_alloc();
        const int ret = av_hwframe_get_buffer(m_framesRef, frame, 0);
        if (ret < 0) {
            av_frame_free(&frame);
            Log::error(QStringLiteral("Quadro D3D11 indisponível: %1").arg(avError(ret)));
            return false;
        }
        auto *dst = reinterpret_cast<ID3D11Texture2D *>(frame->data[0]);
        const UINT index = UINT(reinterpret_cast<intptr_t>(frame->data[1]));
        m_d3d->CopySubresourceRegion(dst, index, 0, 0, 0, texture, 0, nullptr);
    } else {
        m_d3d->CopyResource(m_staging, texture);
        D3D11_MAPPED_SUBRESOURCE map{};
        if (FAILED(m_d3d->Map(m_staging, 0, D3D11_MAP_READ, 0, &map)))
            return false;
        av_frame_make_writable(m_swFrame);
        const uint8_t *src[4] = {static_cast<const uint8_t *>(map.pData), nullptr, nullptr, nullptr};
        const int stride[4] = {int(map.RowPitch), 0, 0, 0};
        sws_scale(m_sws, src, stride, 0, m_ctx->height, m_swFrame->data, m_swFrame->linesize);
        m_d3d->Unmap(m_staging, 0);
        frame = av_frame_clone(m_swFrame);
    }
    frame->pts = pts;
    if (keyFrame) {
        frame->pict_type = AV_PICTURE_TYPE_I;
        frame->flags |= AV_FRAME_FLAG_KEY;
    } else {
        frame->pict_type = AV_PICTURE_TYPE_NONE;
        frame->flags &= ~AV_FRAME_FLAG_KEY;
    }
    const int ret = avcodec_send_frame(m_ctx, frame);
    av_frame_free(&frame);
    if (ret < 0 && ret != AVERROR(EAGAIN)) {
        Log::error(QStringLiteral("Encoder %1: %2").arg(m_info.label, avError(ret)));
        return false;
    }
    return drain(out);
}

bool VideoEncoder::drain(const Output &out)
{
    for (;;) {
        const int ret = avcodec_receive_packet(m_ctx, m_packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return true;
        if (ret < 0) {
            Log::error(QStringLiteral("Encoder %1: %2").arg(m_info.label, avError(ret)));
            return false;
        }
        out(m_packet);
        av_packet_unref(m_packet);
    }
}

void VideoEncoder::flush(const Output &out)
{
    if (avcodec_send_frame(m_ctx, nullptr) >= 0)
        drain(out);
}

} // namespace aeonis
