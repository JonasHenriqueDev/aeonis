using System.ComponentModel;
using System.ComponentModel.Design;
using System.Drawing.Design;
using System.Text.Json;
using System.Text.Json.Serialization;
using Microsoft.Win32;

namespace Aeonis.Core;

public enum CaptureMode { Destaques, PartidaCompleta }

public enum EncoderChoice { Auto, NvidiaH264, NvidiaHevc, AmdH264, IntelH264, CpuX264 }

public sealed class Settings
{
    // ---- Gravação ----
    [Category("1. Gravação"), DisplayName("Pasta dos vídeos")]
    [Description("Onde os clipes e partidas são salvos (uma subpasta por jogo).")]
    [Editor(typeof(System.Windows.Forms.Design.FolderNameEditor), typeof(UITypeEditor))]
    public string OutputDir { get; set; } =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyVideos), "Aeonis");

    [Category("1. Gravação"), DisplayName("Modo")]
    [Description("Destaques: mantém só um buffer curto e salva clipes. PartidaCompleta: grava a partida inteira e também gera os clipes de destaque.")]
    public CaptureMode Mode { get; set; } = CaptureMode.Destaques;

    [Category("1. Gravação"), DisplayName("Encoder de vídeo")]
    [Description("Auto escolhe o encoder de hardware disponível (NVIDIA > AMD > Intel). CPU x264 é o único que pesa no processador.")]
    public EncoderChoice Encoder { get; set; } = EncoderChoice.Auto;

    [Category("1. Gravação"), DisplayName("FPS")]
    [Description("Quadros por segundo da gravação (30 ou 60 recomendados).")]
    public int Fps { get; set; } = 60;

    [Category("1. Gravação"), DisplayName("Bitrate (Mbps)")]
    [Description("Qualidade/tamanho. 1080p60: 15–25 Mbps. 1440p: 25–40 Mbps.")]
    public int BitrateMbps { get; set; } = 20;

    [Category("1. Gravação"), DisplayName("Monitor")]
    [Description("-1 = monitor onde o jogo está. 0 = principal, 1 = segundo monitor...")]
    public int MonitorIndex { get; set; } = -1;

    [Category("1. Gravação"), DisplayName("Mostrar cursor")]
    public bool DrawMouse { get; set; } = false;

    [Category("1. Gravação"), DisplayName("Buffer sempre ativo")]
    [Description("Mantém o replay buffer rodando mesmo sem jogo detectado (grava a área de trabalho).")]
    public bool AlwaysOnBuffer { get; set; } = false;

    // ---- Clipes ----
    [Category("2. Clipes"), DisplayName("Duração do clipe manual (s)")]
    [Description("Quantos segundos para trás o atalho de salvar clipe captura.")]
    public int ReplaySeconds { get; set; } = 30;

    [Category("2. Clipes"), DisplayName("Segundos antes do destaque")]
    public int ClipPreSeconds { get; set; } = 15;

    [Category("2. Clipes"), DisplayName("Segundos depois do destaque")]
    public int ClipPostSeconds { get; set; } = 5;

    [Category("2. Clipes"), DisplayName("Destaques automáticos")]
    [Description("Salva clipes sozinho em abates/multikills (League of Legends e CS2, via APIs oficiais locais).")]
    public bool AutoHighlights { get; set; } = true;

    [Category("2. Clipes"), DisplayName("Clipar mortes")]
    public bool ClipDeaths { get; set; } = false;

    [Category("2. Clipes"), DisplayName("Clipar assistências")]
    public bool ClipAssists { get; set; } = false;

    [Category("2. Clipes"), DisplayName("Limite de armazenamento (GB)")]
    [Description("Apaga os vídeos mais antigos da pasta quando o total passar deste valor. 0 = sem limite.")]
    public double MaxStorageGb { get; set; } = 0;

    // ---- Áudio ----
    [Category("3. Áudio"), DisplayName("Áudio do sistema")]
    [Description("Grava o som do jogo/PC (WASAPI loopback).")]
    public bool SystemAudio { get; set; } = true;

    [Category("3. Áudio"), DisplayName("Microfone")]
    public bool Microphone { get; set; } = false;

    [Category("3. Áudio"), DisplayName("Nome do microfone")]
    [Description("Parte do nome do dispositivo. Vazio = microfone padrão do Windows.")]
    public string MicDevice { get; set; } = "";

    // ---- Atalhos ----
    [Category("4. Atalhos"), DisplayName("Salvar clipe")]
    [Description("Ex.: Alt+F10, Ctrl+Shift+S")]
    public string HotkeySaveClip { get; set; } = "Alt+F10";

    [Category("4. Atalhos"), DisplayName("Iniciar/parar gravação")]
    public string HotkeyToggleRecording { get; set; } = "Alt+F9";

    // ---- Geral ----
    [Category("5. Geral"), DisplayName("Iniciar com o Windows")]
    public bool StartWithWindows { get; set; } = false;

    [Category("5. Geral"), DisplayName("Som ao salvar clipe")]
    public bool NotifySound { get; set; } = true;

    [Category("5. Geral"), DisplayName("Caminho do FFmpeg")]
    [Description("Vazio = procura ao lado do Aeonis.exe, em %LOCALAPPDATA%\\Aeonis\\ffmpeg e no PATH. Requer FFmpeg 6.1+.")]
    [Editor(typeof(System.Windows.Forms.Design.FileNameEditor), typeof(UITypeEditor))]
    public string FfmpegPath { get; set; } = "";

    [Category("5. Geral"), DisplayName("Jogos personalizados")]
    [Description("Um por linha: Nome=executavel.exe;outro.exe")]
    [Editor(typeof(MultilineStringEditor), typeof(UITypeEditor))]
    public string CustomGames { get; set; } = "";

    static readonly JsonSerializerOptions Json = new()
    {
        WriteIndented = true,
        Converters = { new JsonStringEnumConverter() },
    };

    public static Settings Load()
    {
        try
        {
            if (File.Exists(Paths.SettingsFile))
                return (JsonSerializer.Deserialize<Settings>(File.ReadAllText(Paths.SettingsFile), Json) ?? new()).Normalize();
        }
        catch (Exception ex) { Log.Error("Falha ao ler configurações; usando padrões", ex); }
        return new Settings();
    }

    public void Save()
    {
        Normalize();
        File.WriteAllText(Paths.SettingsFile, JsonSerializer.Serialize(this, Json));
    }

    public Settings Clone() => JsonSerializer.Deserialize<Settings>(JsonSerializer.Serialize(this, Json), Json)!;

    public Settings Normalize()
    {
        Fps = Math.Clamp(Fps, 10, 240);
        BitrateMbps = Math.Clamp(BitrateMbps, 2, 150);
        ReplaySeconds = Math.Clamp(ReplaySeconds, 5, 600);
        ClipPreSeconds = Math.Clamp(ClipPreSeconds, 0, 120);
        ClipPostSeconds = Math.Clamp(ClipPostSeconds, 0, 60);
        MonitorIndex = Math.Max(-1, MonitorIndex);
        MaxStorageGb = Math.Max(0, MaxStorageGb);
        if (string.IsNullOrWhiteSpace(OutputDir)) OutputDir = new Settings().OutputDir;
        return this;
    }

    public static void ApplyStartup(bool enabled)
    {
        try
        {
            using var key = Registry.CurrentUser.OpenSubKey(@"Software\Microsoft\Windows\CurrentVersion\Run", writable: true);
            if (key == null) return;
            if (enabled && Environment.ProcessPath is { } exe) key.SetValue("Aeonis", $"\"{exe}\" --minimized");
            else key.DeleteValue("Aeonis", throwOnMissingValue: false);
        }
        catch (Exception ex) { Log.Error("Falha ao configurar inicialização com o Windows", ex); }
    }
}
