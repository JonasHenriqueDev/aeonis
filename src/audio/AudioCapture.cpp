#include "AudioCapture.h"
#include "core/Log.h"

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>

using Microsoft::WRL::ComPtr;

namespace aeonis {

namespace {

constexpr size_t RingFrames = AudioCapture::SampleRate * 2; // 2 s

qint64 qpcNow()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

QString friendlyName(IMMDevice *device)
{
    ComPtr<IPropertyStore> props;
    QString name;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
        PROPVARIANT pv;
        PropVariantInit(&pv);
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv)) && pv.vt == VT_LPWSTR)
            name = QString::fromWCharArray(pv.pwszVal);
        PropVariantClear(&pv);
    }
    return name;
}

QString deviceId(IMMDevice *device)
{
    LPWSTR id = nullptr;
    QString result;
    if (SUCCEEDED(device->GetId(&id)))
        result = QString::fromWCharArray(id);
    CoTaskMemFree(id);
    return result;
}

ComPtr<IMMDevice> findDevice(IMMDeviceEnumerator *en, AudioCapture::Kind kind, const QString &hint)
{
    ComPtr<IMMDevice> device;
    if (kind == AudioCapture::Kind::Microphone && !hint.trimmed().isEmpty()) {
        ComPtr<IMMDeviceCollection> all;
        UINT count = 0;
        if (SUCCEEDED(en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &all)) && SUCCEEDED(all->GetCount(&count))) {
            for (UINT i = 0; i < count; ++i) {
                ComPtr<IMMDevice> d;
                if (SUCCEEDED(all->Item(i, &d)) && friendlyName(d.Get()).contains(hint.trimmed(), Qt::CaseInsensitive))
                    return d;
            }
        }
        Log::warn(QStringLiteral("Microfone '%1' não encontrado; usando o padrão.").arg(hint));
    }
    en->GetDefaultAudioEndpoint(kind == AudioCapture::Kind::Loopback ? eRender : eCapture, eConsole, &device);
    return device;
}

} // namespace

AudioCapture::AudioCapture(Kind kind, const QString &deviceHint) : m_kind(kind), m_hint(deviceHint)
{
    m_ring.resize(RingFrames * Channels);
}

AudioCapture::~AudioCapture() { stop(); }

QStringList AudioCapture::microphones()
{
    QStringList names;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        ComPtr<IMMDeviceEnumerator> en;
        ComPtr<IMMDeviceCollection> all;
        UINT count = 0;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))
            && SUCCEEDED(en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &all)) && SUCCEEDED(all->GetCount(&count))) {
            for (UINT i = 0; i < count; ++i) {
                ComPtr<IMMDevice> d;
                if (SUCCEEDED(all->Item(i, &d)))
                    names << friendlyName(d.Get());
            }
        }
    }
    if (SUCCEEDED(init))
        CoUninitialize();
    return names;
}

bool AudioCapture::start(QString *error)
{
    stop();
    {
        std::lock_guard lock(m_mutex);
        m_head = m_count = 0;
    }
    m_running = true;
    std::promise<bool> opened;
    auto ready = opened.get_future();
    m_thread = std::thread(&AudioCapture::run, this, &opened);
    const bool ok = ready.get();
    if (!ok) {
        stop();
        if (error) *error = m_error;
    }
    return ok;
}

void AudioCapture::stop()
{
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
}

QString AudioCapture::deviceName() const
{
    std::lock_guard lock(m_mutex);
    return m_deviceName;
}

size_t AudioCapture::available() const
{
    std::lock_guard lock(m_mutex);
    return m_count;
}

size_t AudioCapture::read(float *dst, size_t frames)
{
    std::lock_guard lock(m_mutex);
    const size_t n = std::min(frames, m_count);
    for (size_t i = 0; i < n; ++i) {
        const size_t idx = ((m_head + i) % RingFrames) * Channels;
        dst[i * Channels] = m_ring[idx];
        dst[i * Channels + 1] = m_ring[idx + 1];
    }
    m_head = (m_head + n) % RingFrames;
    m_count -= n;
    return n;
}

void AudioCapture::discard(size_t frames)
{
    std::lock_guard lock(m_mutex);
    const size_t n = std::min(frames, m_count);
    m_head = (m_head + n) % RingFrames;
    m_count -= n;
}

void AudioCapture::push(const float *data, size_t frames)
{
    std::lock_guard lock(m_mutex);
    for (size_t i = 0; i < frames; ++i) {
        if (m_count == RingFrames) { // cheio: descarta o mais antigo
            m_head = (m_head + 1) % RingFrames;
            --m_count;
        }
        const size_t idx = ((m_head + m_count) % RingFrames) * Channels;
        if (data) {
            m_ring[idx] = data[i * Channels];
            m_ring[idx + 1] = data[i * Channels + 1];
        } else {
            m_ring[idx] = m_ring[idx + 1] = 0.f;
        }
        ++m_count;
    }
}

void AudioCapture::run(std::promise<bool> *opened)
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const char *tag = m_kind == Kind::Loopback ? "sistema" : "microfone";
    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    fmt.nChannels = Channels;
    fmt.nSamplesPerSec = SampleRate;
    fmt.wBitsPerSample = 32;
    fmt.nBlockAlign = fmt.nChannels * fmt.wBitsPerSample / 8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    bool first = true;
    auto report = [&](bool ok, const QString &err) {
        if (!first)
            return;
        first = false;
        m_error = err;
        opened->set_value(ok);
    };

    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en)))) {
        report(false, QStringLiteral("MMDeviceEnumerator indisponível"));
        CoUninitialize();
        return;
    }

    while (m_running) {
        ComPtr<IMMDevice> device = findDevice(en.Get(), m_kind, m_hint);
        ComPtr<IAudioClient> client;
        ComPtr<IAudioCaptureClient> capture;
        HRESULT h = device ? device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client) : E_FAIL;
        DWORD flags = AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        if (m_kind == Kind::Loopback)
            flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
        if (SUCCEEDED(h))
            h = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 2'000'000 /* 200 ms */, 0, &fmt, nullptr);
        if (SUCCEEDED(h))
            h = client->GetService(IID_PPV_ARGS(&capture));
        if (SUCCEEDED(h))
            h = client->Start();
        if (FAILED(h)) {
            const QString err = QStringLiteral("áudio do %1 indisponível (0x%2)").arg(QLatin1String(tag))
                                    .arg(quint32(h), 8, 16, QLatin1Char('0'));
            if (first) {
                report(false, err);
                break;
            }
            Log::warn(err);
            for (int i = 0; i < 20 && m_running; ++i)
                Sleep(100); // tenta de novo (dispositivo trocado/desconectado)
            continue;
        }
        const QString id = deviceId(device.Get());
        {
            std::lock_guard lock(m_mutex);
            m_deviceName = friendlyName(device.Get());
        }
        Log::info(QStringLiteral("Áudio do %1: %2").arg(QLatin1String(tag), deviceName()));
        report(true, {});

        ULONGLONG nextCheck = GetTickCount64() + 2000;
        bool reopen = false;
        while (m_running && !reopen) {
            Sleep(10);
            UINT32 frames = 0;
            while (SUCCEEDED(h = capture->GetNextPacketSize(&frames)) && frames > 0) {
                BYTE *data = nullptr;
                DWORD bufferFlags = 0;
                h = capture->GetBuffer(&data, &frames, &bufferFlags, nullptr, nullptr);
                if (FAILED(h))
                    break;
                const bool silent = bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT;
                push(silent ? nullptr : reinterpret_cast<const float *>(data), frames);
                if (!silent)
                    m_lastData = qpcNow();
                capture->ReleaseBuffer(frames);
            }
            if (FAILED(h))
                reopen = true; // AUDCLNT_E_DEVICE_INVALIDATED etc.
            if (m_kind == Kind::Loopback && GetTickCount64() >= nextCheck) {
                nextCheck = GetTickCount64() + 2000;
                ComPtr<IMMDevice> current;
                if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &current)) && deviceId(current.Get()) != id)
                    reopen = true; // o usuário trocou a saída de som padrão
            }
        }
        client->Stop();
    }
    report(false, m_error.isEmpty() ? QStringLiteral("captura de áudio interrompida") : m_error);
    CoUninitialize();
}

} // namespace aeonis
