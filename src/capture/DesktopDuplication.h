#pragma once
// Captura de tela pela DXGI Desktop Duplication (a mesma API do ddagrab do FFmpeg), com os quadros
// sempre na GPU. O dispositivo D3D11 é criado no adaptador dono do monitor e é o mesmo que o encoder
// usa, então NVENC/AMF leem a textura direto (zero-copy).
//
// O tamanho de saída é fixado na abertura. Se o modo de vídeo mudar no meio (jogo em tela cheia
// exclusiva com outra resolução), o quadro é redimensionado na GPU para o tamanho original, para
// que todos os segmentos da sessão tenham os mesmos parâmetros e possam ser concatenados.

#include <QSize>
#include <QString>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

namespace aeonis {

class DesktopDuplication {
public:
    enum class Result { NewFrame, Timeout, Lost };

    DesktopDuplication() = default;
    ~DesktopDuplication();
    DesktopDuplication(const DesktopDuplication &) = delete;
    DesktopDuplication &operator=(const DesktopDuplication &) = delete;

    bool open(HMONITOR monitor, QString *error);
    /// Recria a duplicação depois de ACCESS_LOST (UAC, tela bloqueada, troca de resolução).
    bool reopen();
    void close();

    /// Espera até `timeoutMs` por um quadro novo e o copia para a imagem atual.
    Result acquire(int timeoutMs);
    /// Imagem atual no tamanho de saída (BGRA), com o cursor se `drawCursor`.
    ID3D11Texture2D *compose(bool drawCursor);

    ID3D11Device *device() const { return m_device.Get(); }
    ID3D11DeviceContext *context() const { return m_context.Get(); }
    QSize size() const { return m_size; }
    QString adapterName() const { return m_adapterName; }
    LUID adapterLuid() const { return m_luid; }

private:
    bool duplicate();
    bool ensureScaler(const D3D11_TEXTURE2D_DESC &src);
    void copyToCurrent(ID3D11Texture2D *frame);
    void drawPointer(ID3D11Texture2D *target);

    HMONITOR m_monitor = nullptr;
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGIOutput1> m_output;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> m_dupl;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_current;  // tamanho de saída
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_composed; // m_current + cursor (GDI-compatível)
    // Redimensionamento quando o modo de vídeo muda.
    Microsoft::WRL::ComPtr<ID3D11VideoDevice> m_videoDevice;
    Microsoft::WRL::ComPtr<ID3D11VideoContext> m_videoContext;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> m_vpEnum;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> m_vp;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> m_vpOut;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_vpInput;
    QSize m_scalerSource;
    bool m_scalerFailed = false;

    QSize m_size;
    RECT m_desktop{};
    QString m_adapterName;
    LUID m_luid{};
    bool m_frameHeld = false;
};

} // namespace aeonis
