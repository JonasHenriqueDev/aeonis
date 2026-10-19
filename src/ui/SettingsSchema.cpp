#include "SettingsSchema.h"
#include "app/Hotkeys.h"
#include "audio/AudioCapture.h"
#include "capture/Monitors.h"

namespace aeonis::SettingsSchema {

namespace {

QVariantMap option(const QVariant &value, const QString &label)
{
    return {{QStringLiteral("value"), value}, {QStringLiteral("label"), label}};
}

QVariantMap field(const char *key, const char *category, const char *label, const char *description, const char *type,
                  const QVariant &value, const QVariantList &options = {})
{
    QVariantMap f;
    f.insert(QStringLiteral("key"), QLatin1String(key));
    f.insert(QStringLiteral("category"), QString::fromUtf8(category));
    f.insert(QStringLiteral("label"), QString::fromUtf8(label));
    f.insert(QStringLiteral("description"), QString::fromUtf8(description));
    f.insert(QStringLiteral("type"), QLatin1String(type));
    f.insert(QStringLiteral("value"), value);
    f.insert(QStringLiteral("options"), options);
    return f;
}

} // namespace

QVariantList fields(const Settings &s)
{
    QVariantList monitors{option(-1, QStringLiteral("Onde o jogo está (automático)"))};
    const auto list = Monitors::list();
    for (int i = 0; i < list.size(); ++i)
        monitors.append(option(i, QStringLiteral("%1 — %2x%3").arg(i == 0 ? QStringLiteral("Principal") : QStringLiteral("Monitor %1").arg(i + 1))
                                      .arg(list[i].width).arg(list[i].height)));
    if (s.monitorIndex >= list.size())
        monitors.append(option(s.monitorIndex, QStringLiteral("Monitor %1 (desconectado)").arg(s.monitorIndex + 1)));

    QVariantList mics{option(QString(), QStringLiteral("Padrão do Windows"))};
    for (const QString &m : AudioCapture::microphones())
        mics.append(option(m, m));
    if (!s.micDevice.isEmpty() && !AudioCapture::microphones().contains(s.micDevice))
        mics.append(option(s.micDevice, s.micDevice + QStringLiteral(" (não encontrado)")));

    const QVariantList modes{option(QStringLiteral("Destaques"), QStringLiteral("Somente destaques")),
                             option(QStringLiteral("PartidaCompleta"), QStringLiteral("Partida completa + destaques"))};
    const QVariantList encoders{
        option(QStringLiteral("Auto"), QStringLiteral("Automático (recomendado)")),
        option(QStringLiteral("NvidiaH264"), QStringLiteral("NVIDIA NVENC H.264")),
        option(QStringLiteral("NvidiaHevc"), QStringLiteral("NVIDIA NVENC HEVC (H.265)")),
        option(QStringLiteral("AmdH264"), QStringLiteral("AMD AMF H.264")),
        option(QStringLiteral("IntelH264"), QStringLiteral("Intel Quick Sync H.264")),
        option(QStringLiteral("CpuX264"), QStringLiteral("CPU x264 (pesado)")),
    };
    QVariantList fpsOptions;
    for (int f : {30, 60, 120, 144})
        fpsOptions.append(option(f, QStringLiteral("%1 fps").arg(f)));
    if (s.fps != 30 && s.fps != 60 && s.fps != 120 && s.fps != 144)
        fpsOptions.append(option(s.fps, QStringLiteral("%1 fps").arg(s.fps)));

    QVariantList out;
    out << field("OutputDir", "Gravação", "Pasta dos vídeos", "Onde os clipes e partidas são salvos (uma subpasta por jogo).", "folder", s.outputDir)
        << field("Mode", "Gravação", "Modo", "Somente destaques: mantém só um buffer curto e salva clipes. Partida completa: grava a partida inteira e também gera os clipes de destaque.", "select", toString(s.mode), modes)
        << field("Encoder", "Gravação", "Encoder de vídeo", "Automático escolhe o encoder de hardware disponível (NVIDIA > AMD > Intel). CPU x264 é o único que pesa no processador.", "select", toString(s.encoder), encoders)
        << field("Fps", "Gravação", "FPS", "Quadros por segundo da gravação (30 ou 60 recomendados).", "select", s.fps, fpsOptions)
        << field("BitrateMbps", "Gravação", "Bitrate (Mbps)", "Qualidade/tamanho. 1080p60: 15–25 Mbps. 1440p: 25–40 Mbps.", "int", s.bitrateMbps)
        << field("MonitorIndex", "Gravação", "Monitor", "Qual tela gravar.", "select", s.monitorIndex, monitors)
        << field("DrawMouse", "Gravação", "Mostrar cursor", "Desenha o ponteiro do mouse no vídeo.", "bool", s.drawMouse)
        << field("AlwaysOnBuffer", "Gravação", "Buffer sempre ativo", "Mantém o replay buffer rodando mesmo sem jogo detectado (grava a área de trabalho).", "bool", s.alwaysOnBuffer)
        << field("ReplaySeconds", "Clipes", "Duração do clipe manual (s)", "Quantos segundos para trás o atalho de salvar clipe captura.", "int", s.replaySeconds)
        << field("ClipPreSeconds", "Clipes", "Segundos antes do destaque", "", "int", s.clipPreSeconds)
        << field("ClipPostSeconds", "Clipes", "Segundos depois do destaque", "", "int", s.clipPostSeconds)
        << field("AutoHighlights", "Clipes", "Destaques automáticos", "Salva clipes sozinho em abates/multikills (League of Legends e CS2, via APIs oficiais locais).", "bool", s.autoHighlights)
        << field("ClipDeaths", "Clipes", "Clipar mortes", "", "bool", s.clipDeaths)
        << field("ClipAssists", "Clipes", "Clipar assistências", "", "bool", s.clipAssists)
        << field("MaxStorageGb", "Clipes", "Limite de armazenamento (GB)", "Quando os vídeos não salvos passarem deste tamanho, os mais antigos são apagados automaticamente. Vídeos marcados como salvos na biblioteca (★) nunca são apagados e não contam para o limite. 0 = sem limite.", "number", s.maxStorageGb)
        << field("SystemAudio", "Áudio", "Áudio do sistema", "Grava o som do jogo/PC (WASAPI loopback).", "bool", s.systemAudio)
        << field("Microphone", "Áudio", "Microfone", "", "bool", s.microphone)
        << field("MicDevice", "Áudio", "Dispositivo do microfone", "", "select", s.micDevice, mics)
        << field("HotkeySaveClip", "Atalhos", "Salvar clipe", "Clique e pressione a combinação (ex.: Alt+F10).", "hotkey", s.hotkeySaveClip)
        << field("HotkeyToggleRecording", "Atalhos", "Iniciar/parar gravação", "", "hotkey", s.hotkeyToggleRecording)
        << field("StartWithWindows", "Geral", "Iniciar com o Windows", "Abre só o ícone na bandeja ao entrar no Windows.", "bool", s.startWithWindows)
        << field("NotifySound", "Geral", "Som ao salvar clipe", "", "bool", s.notifySound)
        << field("AutoUpdate", "Geral", "Verificar atualizações", "Consulta o GitHub uma vez por dia e avisa quando houver versão nova.", "bool", s.autoUpdate)
        << field("CustomGames", "Geral", "Jogos personalizados", "Um por linha: Nome=executavel.exe;outro.exe", "multiline", s.customGames);
    return out;
}

bool apply(Settings &s, const QVariantMap &values, QString *error)
{
    Settings u = s;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const QString &k = it.key();
        const QVariant &v = it.value();
        if (k == QLatin1String("OutputDir")) u.outputDir = v.toString();
        else if (k == QLatin1String("Mode")) u.mode = captureModeFrom(v.toString());
        else if (k == QLatin1String("Encoder")) u.encoder = encoderChoiceFrom(v.toString());
        else if (k == QLatin1String("Fps")) u.fps = v.toInt();
        else if (k == QLatin1String("BitrateMbps")) u.bitrateMbps = v.toInt();
        else if (k == QLatin1String("MonitorIndex")) u.monitorIndex = v.toInt();
        else if (k == QLatin1String("DrawMouse")) u.drawMouse = v.toBool();
        else if (k == QLatin1String("AlwaysOnBuffer")) u.alwaysOnBuffer = v.toBool();
        else if (k == QLatin1String("ReplaySeconds")) u.replaySeconds = v.toInt();
        else if (k == QLatin1String("ClipPreSeconds")) u.clipPreSeconds = v.toInt();
        else if (k == QLatin1String("ClipPostSeconds")) u.clipPostSeconds = v.toInt();
        else if (k == QLatin1String("AutoHighlights")) u.autoHighlights = v.toBool();
        else if (k == QLatin1String("ClipDeaths")) u.clipDeaths = v.toBool();
        else if (k == QLatin1String("ClipAssists")) u.clipAssists = v.toBool();
        else if (k == QLatin1String("MaxStorageGb")) u.maxStorageGb = v.toDouble();
        else if (k == QLatin1String("SystemAudio")) u.systemAudio = v.toBool();
        else if (k == QLatin1String("Microphone")) u.microphone = v.toBool();
        else if (k == QLatin1String("MicDevice")) u.micDevice = v.toString();
        else if (k == QLatin1String("HotkeySaveClip") || k == QLatin1String("HotkeyToggleRecording")) {
            UINT mods = 0, vk = 0;
            if (!Hotkeys::parse(v.toString(), &mods, &vk)) {
                *error = QStringLiteral("Atalho inválido: %1").arg(v.toString());
                return false;
            }
            (k == QLatin1String("HotkeySaveClip") ? u.hotkeySaveClip : u.hotkeyToggleRecording) = v.toString();
        }
        else if (k == QLatin1String("StartWithWindows")) u.startWithWindows = v.toBool();
        else if (k == QLatin1String("NotifySound")) u.notifySound = v.toBool();
        else if (k == QLatin1String("AutoUpdate")) u.autoUpdate = v.toBool();
        else if (k == QLatin1String("CustomGames")) u.customGames = v.toString();
    }
    if (u.hotkeySaveClip.compare(u.hotkeyToggleRecording, Qt::CaseInsensitive) == 0) {
        *error = QStringLiteral("Os dois atalhos não podem ser iguais.");
        return false;
    }
    s = u.normalize();
    return true;
}

} // namespace aeonis::SettingsSchema
