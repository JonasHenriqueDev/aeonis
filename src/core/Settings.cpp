#include "Settings.h"
#include "Log.h"
#include "Paths.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>

#include <windows.h>

namespace aeonis {

namespace {

const char *const ModeNames[] = {"Destaques", "PartidaCompleta", "SomentePartida"};
const char *const EncoderNames[] = {"Auto", "NvidiaH264", "NvidiaHevc", "AmdH264", "IntelH264", "CpuX264"};

// Chaves que esta versão lê e grava; o resto vai para `extra`.
const char *const KnownKeys[] = {
    "OutputDir", "Mode", "Encoder", "Fps", "BitrateMbps", "MonitorIndex", "DrawMouse", "AlwaysOnBuffer",
    "ReplaySeconds", "ClipPreSeconds", "ClipPostSeconds", "AutoHighlights", "ClipDeaths", "ClipAssists",
    "MaxStorageGb", "SystemAudio", "Microphone", "MicDevice", "HotkeySaveClip", "HotkeyToggleRecording",
    "StartWithWindows", "Notifications", "AutoUpdate", "CustomGames",
};

int readInt(const QJsonObject &o, const char *key, int fallback)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isDouble() ? int(v.toDouble()) : fallback;
}

bool readBool(const QJsonObject &o, const char *key, bool fallback)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isBool() ? v.toBool() : fallback;
}

QString readString(const QJsonObject &o, const char *key, const QString &fallback)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isString() ? v.toString() : fallback;
}

} // namespace

QString toString(CaptureMode m) { return QLatin1String(ModeNames[int(m)]); }
QString toString(EncoderChoice e) { return QLatin1String(EncoderNames[int(e)]); }

CaptureMode captureModeFrom(const QString &s)
{
    for (int i = 0; i < int(std::size(ModeNames)); ++i)
        if (s.compare(QLatin1String(ModeNames[i]), Qt::CaseInsensitive) == 0)
            return CaptureMode(i);
    return CaptureMode::Destaques;
}

EncoderChoice encoderChoiceFrom(const QString &s)
{
    for (int i = 0; i < int(std::size(EncoderNames)); ++i)
        if (s.compare(QLatin1String(EncoderNames[i]), Qt::CaseInsensitive) == 0)
            return EncoderChoice(i);
    return EncoderChoice::Auto;
}

Settings::Settings() : outputDir(Paths::defaultVideos()) {}

Settings Settings::fromJson(const QJsonObject &o)
{
    Settings s;
    s.outputDir = readString(o, "OutputDir", s.outputDir);
    s.mode = captureModeFrom(readString(o, "Mode", toString(s.mode)));
    s.encoder = encoderChoiceFrom(readString(o, "Encoder", toString(s.encoder)));
    s.fps = readInt(o, "Fps", s.fps);
    s.bitrateMbps = readInt(o, "BitrateMbps", s.bitrateMbps);
    s.monitorIndex = readInt(o, "MonitorIndex", s.monitorIndex);
    s.drawMouse = readBool(o, "DrawMouse", s.drawMouse);
    s.alwaysOnBuffer = readBool(o, "AlwaysOnBuffer", s.alwaysOnBuffer);
    s.replaySeconds = readInt(o, "ReplaySeconds", s.replaySeconds);
    s.clipPreSeconds = readInt(o, "ClipPreSeconds", s.clipPreSeconds);
    s.clipPostSeconds = readInt(o, "ClipPostSeconds", s.clipPostSeconds);
    s.autoHighlights = readBool(o, "AutoHighlights", s.autoHighlights);
    s.clipDeaths = readBool(o, "ClipDeaths", s.clipDeaths);
    s.clipAssists = readBool(o, "ClipAssists", s.clipAssists);
    if (o.value(QLatin1String("MaxStorageGb")).isDouble())
        s.maxStorageGb = o.value(QLatin1String("MaxStorageGb")).toDouble();
    s.systemAudio = readBool(o, "SystemAudio", s.systemAudio);
    s.microphone = readBool(o, "Microphone", s.microphone);
    s.micDevice = readString(o, "MicDevice", s.micDevice);
    s.hotkeySaveClip = readString(o, "HotkeySaveClip", s.hotkeySaveClip);
    s.hotkeyToggleRecording = readString(o, "HotkeyToggleRecording", s.hotkeyToggleRecording);
    s.startWithWindows = readBool(o, "StartWithWindows", s.startWithWindows);
    s.notifications = readBool(o, "Notifications", s.notifications);
    s.autoUpdate = readBool(o, "AutoUpdate", s.autoUpdate);
    s.customGames = readString(o, "CustomGames", s.customGames);
    s.extra = o;
    for (const char *key : KnownKeys)
        s.extra.remove(QLatin1String(key));
    return s.normalize();
}

QJsonObject Settings::toJson() const
{
    QJsonObject o = extra;
    o.insert(QStringLiteral("OutputDir"), outputDir);
    o.insert(QStringLiteral("Mode"), toString(mode));
    o.insert(QStringLiteral("Encoder"), toString(encoder));
    o.insert(QStringLiteral("Fps"), fps);
    o.insert(QStringLiteral("BitrateMbps"), bitrateMbps);
    o.insert(QStringLiteral("MonitorIndex"), monitorIndex);
    o.insert(QStringLiteral("DrawMouse"), drawMouse);
    o.insert(QStringLiteral("AlwaysOnBuffer"), alwaysOnBuffer);
    o.insert(QStringLiteral("ReplaySeconds"), replaySeconds);
    o.insert(QStringLiteral("ClipPreSeconds"), clipPreSeconds);
    o.insert(QStringLiteral("ClipPostSeconds"), clipPostSeconds);
    o.insert(QStringLiteral("AutoHighlights"), autoHighlights);
    o.insert(QStringLiteral("ClipDeaths"), clipDeaths);
    o.insert(QStringLiteral("ClipAssists"), clipAssists);
    o.insert(QStringLiteral("MaxStorageGb"), maxStorageGb);
    o.insert(QStringLiteral("SystemAudio"), systemAudio);
    o.insert(QStringLiteral("Microphone"), microphone);
    o.insert(QStringLiteral("MicDevice"), micDevice);
    o.insert(QStringLiteral("HotkeySaveClip"), hotkeySaveClip);
    o.insert(QStringLiteral("HotkeyToggleRecording"), hotkeyToggleRecording);
    o.insert(QStringLiteral("StartWithWindows"), startWithWindows);
    o.insert(QStringLiteral("Notifications"), notifications);
    o.insert(QStringLiteral("AutoUpdate"), autoUpdate);
    o.insert(QStringLiteral("CustomGames"), customGames);
    return o;
}

Settings Settings::load()
{
    QFile f(Paths::settingsFile());
    if (!f.exists())
        return Settings();
    if (!f.open(QIODevice::ReadOnly)) {
        Log::error(QStringLiteral("Falha ao ler configurações; usando padrões"));
        return Settings();
    }
    QJsonParseError err{};
    // A versão C# grava com BOM às vezes; o QJsonDocument não aceita BOM.
    QByteArray data = f.readAll();
    if (data.startsWith("\xEF\xBB\xBF"))
        data.remove(0, 3);
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (!doc.isObject()) {
        Log::error(QStringLiteral("Falha ao ler configurações (%1); usando padrões").arg(err.errorString()));
        return Settings();
    }
    return fromJson(doc.object());
}

bool Settings::save() const
{
    Settings copy = *this;
    copy.normalize();
    QSaveFile f(Paths::settingsFile());
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(copy.toJson()).toJson(QJsonDocument::Indented));
    return f.commit();
}

Settings &Settings::normalize()
{
    fps = std::clamp(fps, 10, 240);
    bitrateMbps = std::clamp(bitrateMbps, 2, 150);
    replaySeconds = std::clamp(replaySeconds, 5, 600);
    clipPreSeconds = std::clamp(clipPreSeconds, 0, 120);
    clipPostSeconds = std::clamp(clipPostSeconds, 0, 60);
    monitorIndex = std::max(-1, monitorIndex);
    maxStorageGb = std::max(0.0, maxStorageGb);
    if (outputDir.trimmed().isEmpty())
        outputDir = Paths::defaultVideos();
    outputDir = QDir::toNativeSeparators(outputDir.trimmed());
    return *this;
}

void Settings::applyStartup(bool enabled)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &key)
        != ERROR_SUCCESS) {
        Log::error(QStringLiteral("Falha ao configurar inicialização com o Windows"));
        return;
    }
    if (enabled) {
        wchar_t exe[MAX_PATH * 2]{};
        GetModuleFileNameW(nullptr, exe, DWORD(std::size(exe)));
        const std::wstring value = L"\"" + std::wstring(exe) + L"\" --minimized";
        RegSetValueExW(key, L"Aeonis", 0, REG_SZ, reinterpret_cast<const BYTE *>(value.c_str()),
                       DWORD((value.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, L"Aeonis");
    }
    RegCloseKey(key);
}

} // namespace aeonis
