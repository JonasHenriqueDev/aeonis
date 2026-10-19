#pragma once
// Configurações do Aeonis em %LOCALAPPDATA%\Aeonis\settings.json, no mesmo formato (chaves PascalCase,
// enums como texto) da versão C#: quem atualiza mantém tudo. Chaves desconhecidas são preservadas.

#include <QJsonObject>
#include <QString>

namespace aeonis {

enum class CaptureMode { Destaques, PartidaCompleta, SomentePartida };
enum class EncoderChoice { Auto, NvidiaH264, NvidiaHevc, AmdH264, IntelH264, CpuX264 };

QString toString(CaptureMode m);
QString toString(EncoderChoice e);
CaptureMode captureModeFrom(const QString &s);
EncoderChoice encoderChoiceFrom(const QString &s);

struct Settings {
    // Gravação
    QString outputDir;
    CaptureMode mode = CaptureMode::Destaques;
    EncoderChoice encoder = EncoderChoice::Auto;
    int fps = 60;
    int bitrateMbps = 10;
    int monitorIndex = -1; // -1 = monitor do jogo, 0 = principal, 1.. = demais
    bool drawMouse = false;
    bool alwaysOnBuffer = false;
    // Clipes
    int replaySeconds = 30;
    int clipPreSeconds = 15;
    int clipPostSeconds = 5;
    bool autoHighlights = true;
    bool clipDeaths = false;
    bool clipAssists = false;
    double maxStorageGb = 50;
    // Áudio
    bool systemAudio = true;
    bool microphone = false;
    QString micDevice;
    // Atalhos
    QString hotkeySaveClip = QStringLiteral("Alt+F10");
    QString hotkeyToggleRecording = QStringLiteral("Alt+F9");
    // Geral
    bool startWithWindows = false;
    bool notifications = false; // balões do Windows (falhas aparecem sempre)
    bool autoUpdate = true;
    bool openMatchOnEnd = true; // abre a janela na partida quando o jogo fecha
    QString customGames;

    QJsonObject extra; // chaves que esta versão não conhece

    Settings();
    static Settings load();
    static Settings fromJson(const QJsonObject &o);
    QJsonObject toJson() const;
    bool save() const;
    Settings &normalize();

    /// HKCU\...\Run\Aeonis = "<exe>" --minimized
    static void applyStartup(bool enabled);
};

} // namespace aeonis
