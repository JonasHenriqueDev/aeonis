#include "DesktopDuplication.h"
#include "core/Log.h"

#include <d3d11_4.h>
#include <dxgi1_5.h>

#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace aeonis {

namespace {

QString hr(HRESULT h) { return QStringLiteral("0x%1").arg(quint32(h), 8, 16, QLatin1Char('0')); }

ComPtr<ID3D11Texture2D> createTexture(ID3D11Device *device, QSize size, UINT bind, UINT misc = 0)
{
    D3D11_TEXTURE2D_DESC d{};
    d.Width = UINT(size.width());
    d.Height = UINT(size.height());
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = bind;
    d.MiscFlags = misc;
    ComPtr<ID3D11Texture2D> tex;
    device->CreateTexture2D(&d, nullptr, &tex);
    return tex;
}

void clearBlack(ID3D11Device *device, ID3D11DeviceContext *ctx, ID3D11Texture2D *tex)
{
    ComPtr<ID3D11RenderTargetView> rtv;
    if (SUCCEEDED(device->CreateRenderTargetView(tex, nullptr, &rtv))) {
        const float black[4] = {0, 0, 0, 1};
        ctx->ClearRenderTargetView(rtv.Get(), black);
    }
}

} // namespace

DesktopDuplication::~DesktopDuplication() { close(); }

bool DesktopDuplication::open(HMONITOR monitor, QString *error)
{
    close();
    m_monitor = monitor;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        *error = QStringLiteral("DXGI indisponível");
        return false;
    }
    ComPtr<IDXGIAdapter1> adapter, fallbackAdapter;
    ComPtr<IDXGIOutput> output, fallbackOutput;
    ComPtr<IDXGIAdapter1> a;
    for (UINT ai = 0; !output && factory->EnumAdapters1(ai, &a) != DXGI_ERROR_NOT_FOUND; ++ai) {
        ComPtr<IDXGIOutput> o;
        for (UINT oi = 0; a->EnumOutputs(oi, &o) != DXGI_ERROR_NOT_FOUND; ++oi) {
            DXGI_OUTPUT_DESC od{};
            o->GetDesc(&od);
            if (!fallbackOutput && od.AttachedToDesktop) {
                fallbackOutput = o;
                fallbackAdapter = a;
            }
            if (od.Monitor == monitor) {
                output = o;
                adapter = a;
                break;
            }
            o.Reset();
        }
        a.Reset();
    }
    if (!output) {
        output = fallbackOutput;
        adapter = fallbackAdapter;
    }
    if (!output) {
        *error = QStringLiteral("nenhum monitor encontrado para capturar");
        return false;
    }
    DXGI_ADAPTER_DESC1 ad{};
    adapter->GetDesc1(&ad);
    m_adapterName = QString::fromWCharArray(ad.Description);
    m_luid = ad.AdapterLuid;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1,
                                        D3D_FEATURE_LEVEL_10_0};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
    HRESULT h = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels, UINT(std::size(levels)),
                                  D3D11_SDK_VERSION, &m_device, nullptr, &m_context);
    if (FAILED(h)) {
        flags &= ~D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
        h = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, levels, UINT(std::size(levels)),
                              D3D11_SDK_VERSION, &m_device, nullptr, &m_context);
    }
    if (FAILED(h)) {
        *error = QStringLiteral("falha ao criar o dispositivo D3D11 (%1)").arg(hr(h));
        return false;
    }
    ComPtr<ID3D11Multithread> mt;
    if (SUCCEEDED(m_device.As(&mt)))
        mt->SetMultithreadProtected(TRUE);
    if (FAILED(output.As(&m_output))) {
        *error = QStringLiteral("IDXGIOutput1 indisponível");
        return false;
    }
    DXGI_OUTPUT_DESC od{};
    m_output->GetDesc(&od);
    m_desktop = od.DesktopCoordinates;
    m_size = QSize(int(m_desktop.right - m_desktop.left) & ~1, int(m_desktop.bottom - m_desktop.top) & ~1);
    if (!duplicate()) {
        // Pode estar temporariamente indisponível (UAC/tela bloqueada): a captura tenta de novo.
        Log::warn(QStringLiteral("Desktop Duplication indisponível por enquanto; tentando de novo durante a captura."));
    }
    m_current = createTexture(m_device.Get(), m_size, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    if (!m_current) {
        *error = QStringLiteral("falha ao criar a textura de captura");
        return false;
    }
    clearBlack(m_device.Get(), m_context.Get(), m_current.Get());
    Log::info(QStringLiteral("Captura DXGI: %1x%2 em %3").arg(m_size.width()).arg(m_size.height()).arg(m_adapterName));
    return true;
}

bool DesktopDuplication::duplicate()
{
    m_dupl.Reset();
    m_frameHeld = false;
    if (!m_output)
        return false;
    HRESULT h = E_FAIL;
    ComPtr<IDXGIOutput5> output5;
    if (SUCCEEDED(m_output.As(&output5))) {
        // Pede BGRA 8 bits: em monitor HDR o Windows converte, em vez de entregar FP16.
        const DXGI_FORMAT formats[] = {DXGI_FORMAT_B8G8R8A8_UNORM};
        h = output5->DuplicateOutput1(m_device.Get(), 0, 1, formats, &m_dupl);
    }
    if (FAILED(h))
        h = m_output->DuplicateOutput(m_device.Get(), &m_dupl);
    if (FAILED(h)) {
        m_dupl.Reset();
        return false;
    }
    DXGI_OUTPUT_DESC od{};
    m_output->GetDesc(&od);
    m_desktop = od.DesktopCoordinates;
    return true;
}

bool DesktopDuplication::reopen() { return duplicate(); }

void DesktopDuplication::close()
{
    if (m_dupl && m_frameHeld)
        m_dupl->ReleaseFrame();
    m_frameHeld = false;
    m_dupl.Reset();
    m_vpOut.Reset();
    m_vp.Reset();
    m_vpEnum.Reset();
    m_vpInput.Reset();
    m_videoContext.Reset();
    m_videoDevice.Reset();
    m_composed.Reset();
    m_current.Reset();
    m_output.Reset();
    m_context.Reset();
    m_device.Reset();
}

DesktopDuplication::Result DesktopDuplication::acquire(int timeoutMs)
{
    if (!m_dupl && !duplicate())
        return Result::Lost;
    DXGI_OUTDUPL_FRAME_INFO info{};
    ComPtr<IDXGIResource> resource;
    const HRESULT h = m_dupl->AcquireNextFrame(UINT(std::max(0, timeoutMs)), &info, &resource);
    if (h == DXGI_ERROR_WAIT_TIMEOUT)
        return Result::Timeout;
    if (FAILED(h)) {
        // ACCESS_LOST: resolução, UAC, tela bloqueada, tela cheia exclusiva...
        m_dupl.Reset();
        return Result::Lost;
    }
    m_frameHeld = true;
    bool updated = false;
    if (info.LastPresentTime.QuadPart != 0) {
        ComPtr<ID3D11Texture2D> frame;
        if (SUCCEEDED(resource.As(&frame))) {
            copyToCurrent(frame.Get());
            updated = true;
        }
    }
    m_dupl->ReleaseFrame();
    m_frameHeld = false;
    return updated ? Result::NewFrame : Result::Timeout;
}

void DesktopDuplication::copyToCurrent(ID3D11Texture2D *frame)
{
    D3D11_TEXTURE2D_DESC d{};
    frame->GetDesc(&d);
    const int w = int(d.Width), h = int(d.Height);
    if ((w & ~1) == m_size.width() && (h & ~1) == m_size.height()) {
        const D3D11_BOX box{0, 0, 0, UINT(m_size.width()), UINT(m_size.height()), 1};
        m_context->CopySubresourceRegion(m_current.Get(), 0, 0, 0, 0, frame, 0, &box);
        return;
    }
    // Modo de vídeo diferente do início da sessão: redimensiona na GPU mantendo a proporção.
    if (ensureScaler(d)) {
        m_context->CopyResource(m_vpInput.Get(), frame);
        D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC ivd{};
        ivd.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11VideoProcessorInputView> in;
        if (SUCCEEDED(m_videoDevice->CreateVideoProcessorInputView(m_vpInput.Get(), m_vpEnum.Get(), &ivd, &in))) {
            const double scale = std::min(double(m_size.width()) / w, double(m_size.height()) / h);
            const int tw = int(w * scale), th = int(h * scale);
            RECT target{(m_size.width() - tw) / 2, (m_size.height() - th) / 2, 0, 0};
            target.right = target.left + tw;
            target.bottom = target.top + th;
            m_videoContext->VideoProcessorSetOutputTargetRect(m_vp.Get(), TRUE, &target);
            D3D11_VIDEO_PROCESSOR_STREAM stream{};
            stream.Enable = TRUE;
            stream.pInputSurface = in.Get();
            if (SUCCEEDED(m_videoContext->VideoProcessorBlt(m_vp.Get(), m_vpOut.Get(), 0, 1, &stream)))
                return;
        }
    }
    // Sem processador de vídeo: copia a parte que cabe.
    clearBlack(m_device.Get(), m_context.Get(), m_current.Get());
    const D3D11_BOX box{0, 0, 0, UINT(std::min(w, m_size.width())), UINT(std::min(h, m_size.height())), 1};
    m_context->CopySubresourceRegion(m_current.Get(), 0, 0, 0, 0, frame, 0, &box);
}

bool DesktopDuplication::ensureScaler(const D3D11_TEXTURE2D_DESC &src)
{
    const QSize source(int(src.Width), int(src.Height));
    if (m_vp && m_scalerSource == source)
        return true;
    if (m_scalerFailed && m_scalerSource == source)
        return false;
    m_scalerSource = source;
    m_scalerFailed = true;
    m_vpOut.Reset();
    m_vp.Reset();
    m_vpEnum.Reset();
    m_vpInput.Reset();
    if (!m_videoDevice && (FAILED(m_device.As(&m_videoDevice)) || FAILED(m_context.As(&m_videoContext))))
        return false;
    D3D11_VIDEO_PROCESSOR_CONTENT_DESC cd{};
    cd.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
    cd.InputWidth = src.Width;
    cd.InputHeight = src.Height;
    cd.OutputWidth = UINT(m_size.width());
    cd.OutputHeight = UINT(m_size.height());
    cd.Usage = D3D11_VIDEO_USAGE_OPTIMAL_SPEED;
    if (FAILED(m_videoDevice->CreateVideoProcessorEnumerator(&cd, &m_vpEnum))
        || FAILED(m_videoDevice->CreateVideoProcessor(m_vpEnum.Get(), 0, &m_vp)))
        return false;
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ovd{};
    ovd.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
    if (FAILED(m_videoDevice->CreateVideoProcessorOutputView(m_current.Get(), m_vpEnum.Get(), &ovd, &m_vpOut)))
        return false;
    m_vpInput = createTexture(m_device.Get(), source, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE);
    if (!m_vpInput)
        return false;
    D3D11_VIDEO_PROCESSOR_COLOR_SPACE cs{};
    cs.RGB_Range = 0; // 0-255
    m_videoContext->VideoProcessorSetStreamColorSpace(m_vp.Get(), 0, &cs);
    m_videoContext->VideoProcessorSetOutputColorSpace(m_vp.Get(), &cs);
    const D3D11_VIDEO_COLOR black{{0, 0, 0, 1}};
    m_videoContext->VideoProcessorSetOutputBackgroundColor(m_vp.Get(), FALSE, &black);
    m_scalerFailed = false;
    Log::info(QStringLiteral("Resolução da tela mudou para %1x%2; redimensionando para %3x%4.")
                  .arg(source.width()).arg(source.height()).arg(m_size.width()).arg(m_size.height()));
    return true;
}

ID3D11Texture2D *DesktopDuplication::compose(bool drawCursor)
{
    if (!drawCursor)
        return m_current.Get();
    if (!m_composed)
        m_composed = createTexture(m_device.Get(), m_size, D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE,
                                   D3D11_RESOURCE_MISC_GDI_COMPATIBLE);
    if (!m_composed)
        return m_current.Get();
    m_context->CopyResource(m_composed.Get(), m_current.Get());
    drawPointer(m_composed.Get());
    return m_composed.Get();
}

void DesktopDuplication::drawPointer(ID3D11Texture2D *target)
{
    CURSORINFO ci{};
    ci.cbSize = sizeof(ci);
    if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING) || !ci.hCursor)
        return;
    ICONINFO ii{};
    int hx = 0, hy = 0;
    if (GetIconInfo(ci.hCursor, &ii)) {
        hx = int(ii.xHotspot);
        hy = int(ii.yHotspot);
        if (ii.hbmMask) DeleteObject(ii.hbmMask);
        if (ii.hbmColor) DeleteObject(ii.hbmColor);
    }
    const int x = ci.ptScreenPos.x - m_desktop.left - hx;
    const int y = ci.ptScreenPos.y - m_desktop.top - hy;
    if (x > m_size.width() || y > m_size.height() || x < -64 || y < -64)
        return;
    ComPtr<IDXGISurface1> surface;
    if (FAILED(target->QueryInterface(IID_PPV_ARGS(&surface))))
        return;
    HDC dc = nullptr;
    if (FAILED(surface->GetDC(FALSE, &dc)))
        return;
    DrawIconEx(dc, x, y, ci.hCursor, 0, 0, 0, nullptr, DI_NORMAL | DI_DEFAULTSIZE);
    surface->ReleaseDC(nullptr);
}

} // namespace aeonis
