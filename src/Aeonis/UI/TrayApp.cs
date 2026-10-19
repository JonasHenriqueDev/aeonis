using System.Diagnostics;
using System.Media;
using System.Reflection;
using Aeonis.Capture;
using Aeonis.Core;
using Aeonis.Games;

namespace Aeonis.UI;

/// <summary>Aplicativo de bandeja: sem janela aberta, o Aeonis só ocupa um ícone e o processo FFmpeg da sessão.</summary>
sealed class TrayApp : ApplicationContext
{
    public const string ShowSignalName = "Aeonis.ShowWindow";
    static readonly string Version = Assembly.GetExecutingAssembly().GetName().Version?.ToString(3) ?? "0";

    Settings settings;
    readonly Control invoker = new();
    readonly NotifyIcon tray;
    readonly Icon iconIdle = AppIcon.Create(false), iconRecording = AppIcon.Create(true);
    readonly CaptureEngine engine;
    readonly Highlighter highlighter;
    readonly GameWatcher watcher;
    readonly HotkeyManager hotkeys = new();
    readonly ToolStripMenuItem miStatus, miSave, miRecord;
    IEventSource? events;
    GameDef? activeGame;
    MainWindow? main;
    readonly EventWaitHandle showSignal = new(false, EventResetMode.AutoReset, ShowSignalName);
    bool exiting;

    public TrayApp(bool minimized)
    {
        _ = invoker.Handle; // garante um handle para marshaling à thread de UI
        settings = Settings.Load();
        Ffmpeg.Locate(settings.FfmpegPath);

        engine = new CaptureEngine(() => settings);
        engine.Failed += msg => OnUi(() => { Notify("Aeonis", msg, ToolTipIcon.Error); UpdateUi(); });
        highlighter = new Highlighter(() => settings, (secs, label) => SaveClipAsync(secs, label, "highlight"));
        watcher = new GameWatcher(() => GameCatalog.All(settings));
        watcher.GameStarted += (g, monitor, exe) => _ = OnGameStartedAsync(g, monitor, exe);
        watcher.GameStopped += g => _ = OnGameStoppedAsync(g);

        miStatus = new ToolStripMenuItem("Aguardando jogo…") { Enabled = false };
        miSave = new ToolStripMenuItem("Salvar clipe agora", null, (_, _) => _ = SaveManualClipAsync());
        miRecord = new ToolStripMenuItem("Iniciar gravação", null, (_, _) => _ = ToggleRecordingAsync());
        var menu = new ContextMenuStrip();
        menu.Items.AddRange(
        [
            miStatus,
            new ToolStripSeparator(),
            miSave,
            miRecord,
            new ToolStripSeparator(),
            new ToolStripMenuItem("Abrir Aeonis", null, (_, _) => ShowMain("matches")) { Font = new Font(menu.Font, FontStyle.Bold) },
            new ToolStripMenuItem("Abrir pasta dos vídeos", null, (_, _) => OpenOutputFolder()),
            new ToolStripMenuItem("Configurações", null, (_, _) => ShowMain("settings")),
            new ToolStripMenuItem("Abrir log", null, (_, _) => OpenLog()),
            new ToolStripSeparator(),
            new ToolStripMenuItem("Sair", null, (_, _) => _ = ExitAsync()),
        ]);

        tray = new NotifyIcon { Icon = iconIdle, Text = "Aeonis", Visible = true, ContextMenuStrip = menu };
        tray.DoubleClick += (_, _) => ShowMain("matches");
        tray.BalloonTipClicked += (_, _) => ShowMain("matches");

        RegisterHotkeys();
        Microsoft.Win32.SystemEvents.SessionEnding += (_, _) => engine.StopAsync().GetAwaiter().GetResult();

        if (Ffmpeg.Exe == null)
        {
            ShowFfmpegMissing();
        }
        else
        {
            Log.Info($"FFmpeg: {Ffmpeg.Exe}");
            _ = Task.Run(CaptureEngine.RecoverLeftoversAsync);
            if (settings.AlwaysOnBuffer) _ = StartEngineAsync("Desktop", Math.Max(0, settings.MonitorIndex), null);
        }
        watcher.Start();
        UpdateUi();
        new Thread(() =>
        {
            while (showSignal.WaitOne() && !exiting) OnUi(() => ShowMain("matches"));
        }) { IsBackground = true, Name = "aeonis-show" }.Start();

        // Aberto pelo usuário (atalho/menu Iniciar): mostra a janela. Com o Windows (--minimized): só a bandeja.
        if (!minimized) ShowMain("matches");
    }

    void OnUi(Action action)
    {
        if (invoker.IsDisposed) return;
        if (invoker.InvokeRequired) invoker.BeginInvoke(action);
        else action();
    }

    void Notify(string title, string text, ToolTipIcon icon = ToolTipIcon.Info) =>
        OnUi(() => tray.ShowBalloonTip(3000, title, text, icon));

    // ---------------- Ciclo de vida da captura ----------------

    async Task StartEngineAsync(string game, int monitor, string? exe)
    {
        if (Ffmpeg.Exe == null) { ShowFfmpegMissing(); return; }
        try
        {
            await engine.StartAsync(game, monitor);
            Library.SessionStarted(engine.SessionId, game, exe);
            if (exe != null) _ = Task.Run(() => MediaCache.SaveGameIcon(game, exe));
        }
        catch (Exception ex)
        {
            Log.Error("Falha ao iniciar captura", ex);
            Notify("Falha ao iniciar a captura", ex.Message, ToolTipIcon.Error);
        }
        OnUi(UpdateUi);
    }

    async Task OnGameStartedAsync(GameDef game, int monitor, string? exe)
    {
        activeGame = game;
        if (engine.Running) await StopEngineAndReportAsync();
        await StartEngineAsync(game.Name, settings.MonitorIndex >= 0 ? settings.MonitorIndex : monitor, exe);
        if (!engine.Running) return;

        if (settings.Mode == CaptureMode.PartidaCompleta) engine.BeginRecording();
        if (settings.AutoHighlights)
        {
            events = game.Provider switch
            {
                "lol" => new LeagueEventSource(() => settings),
                "cs2" => new Cs2EventSource(() => settings, exe, (t, m) => Notify(t, m)),
                _ => null,
            };
            if (events != null)
            {
                events.Highlight += reason =>
                {
                    Library.Event(engine.SessionId, reason);
                    highlighter.Trigger(reason);
                };
                events.Start();
            }
        }
        var auto = events != null ? " Destaques automáticos ativos." : "";
        Notify($"{game.Name} detectado", $"Gravando com {engine.EncoderLabel}. {settings.HotkeySaveClip} salva um clipe.{auto}");
        OnUi(UpdateUi);
    }

    async Task OnGameStoppedAsync(GameDef game)
    {
        events?.Dispose();
        events = null;
        await highlighter.FlushAsync();
        await StopEngineAndReportAsync();
        activeGame = null;
        if (settings.AlwaysOnBuffer && !exiting) await StartEngineAsync("Desktop", Math.Max(0, settings.MonitorIndex), null);
        OnUi(UpdateUi);
    }

    async Task StopEngineAndReportAsync()
    {
        var session = engine.SessionId;
        var recordingStart = engine.RecordingSince;
        var requestedAt = DateTime.Now;
        var wasRunning = engine.Running;
        var recording = await engine.StopAsync();
        if (recording != null)
        {
            await Library.ClipSavedAsync(session, recording, "full", LabelOf(recording), requestedAt, recordingStart);
            Notify("Partida salva", Path.GetFileName(recording));
            Clips.EnforceQuota(settings.OutputDir, settings.MaxStorageGb);
        }
        if (wasRunning) Library.SessionEnded(session);
        OnUi(() => main?.RefreshLibrary());
    }

    static string LabelOf(string path)
    {
        var name = Path.GetFileNameWithoutExtension(path);
        return name.Contains("_Gravação") ? "Gravação" : name.Contains("_Recuperado") ? "Recuperado" : "Partida";
    }

    Task SaveManualClipAsync()
    {
        if (!engine.Running)
        {
            Notify("Nada sendo gravado", "Abra um jogo ou ative \"Buffer sempre ativo\" nas configurações.", ToolTipIcon.Warning);
            return Task.CompletedTask;
        }
        return SaveClipAsync(settings.ReplaySeconds, "Clipe", "manual");
    }

    async Task SaveClipAsync(int seconds, string label, string kind)
    {
        if (!engine.Running) return;
        var session = engine.SessionId;
        var requestedAt = DateTime.Now;
        var path = await engine.SaveClipAsync(seconds, label);
        if (path == null)
        {
            Notify("Falha ao salvar clipe", "Veja o log para detalhes.", ToolTipIcon.Error);
            return;
        }
        await Library.ClipSavedAsync(session, path, kind, label, requestedAt);
        if (settings.NotifySound) SystemSounds.Asterisk.Play();
        Notify(kind == "manual" ? "Clipe salvo" : $"Destaque salvo: {label}", Path.GetFileName(path));
        Clips.EnforceQuota(settings.OutputDir, settings.MaxStorageGb);
        OnUi(() => main?.RefreshLibrary());
    }

    async Task ToggleRecordingAsync()
    {
        if (!engine.Running)
        {
            await StartEngineAsync(activeGame?.Name ?? "Desktop", Math.Max(0, settings.MonitorIndex), null);
            if (!engine.Running) return;
            engine.BeginRecording();
            Notify("Gravação iniciada", $"{settings.HotkeyToggleRecording} para parar.");
        }
        else if (engine.IsRecording)
        {
            var session = engine.SessionId;
            var start = engine.RecordingSince;
            var requestedAt = DateTime.Now;
            var path = await engine.EndRecordingAsync();
            if (path != null)
            {
                await Library.ClipSavedAsync(session, path, "full", LabelOf(path), requestedAt, start);
                Notify("Gravação salva", Path.GetFileName(path));
            }
            else Notify("Falha ao salvar gravação", "Veja o log para detalhes.", ToolTipIcon.Error);
            OnUi(() => main?.RefreshLibrary());
            if (activeGame == null && !settings.AlwaysOnBuffer) await StopEngineAndReportAsync();
        }
        else
        {
            engine.BeginRecording();
            Notify("Gravação iniciada", $"{settings.HotkeyToggleRecording} para parar.");
        }
        OnUi(UpdateUi);
    }

    StatusDto Status() => new(
        !engine.Running ? "idle" : engine.IsRecording ? "recording" : "buffer",
        engine.Game, engine.EncoderLabel, settings.HotkeySaveClip, settings.HotkeyToggleRecording, Version);

    void UpdateUi()
    {
        string state = !engine.Running ? "Aguardando jogo…"
            : engine.IsRecording ? $"● Gravando: {engine.Game}"
            : $"Buffer ativo: {engine.Game}";
        miStatus.Text = state;
        miSave.Text = $"Salvar clipe agora ({settings.HotkeySaveClip})";
        miSave.Enabled = engine.Running;
        miRecord.Text = (engine.IsRecording ? "Parar gravação" : "Iniciar gravação") + $" ({settings.HotkeyToggleRecording})";
        tray.Icon = engine.IsRecording ? iconRecording : iconIdle;
        var tip = "Aeonis — " + state;
        tray.Text = tip.Length > 63 ? tip[..63] : tip;
        main?.PushStatus();
    }

    // ---------------- Janelas e utilidades ----------------

    void RegisterHotkeys()
    {
        hotkeys.UnregisterAll();
        var errors = new List<string>();
        if (!hotkeys.Register(settings.HotkeySaveClip, () => _ = SaveManualClipAsync(), out var e1)) errors.Add(e1!);
        if (!hotkeys.Register(settings.HotkeyToggleRecording, () => _ = ToggleRecordingAsync(), out var e2)) errors.Add(e2!);
        if (errors.Count > 0) Notify("Atalhos", string.Join("\n", errors), ToolTipIcon.Warning);
    }

    void ShowMain(string page)
    {
        if (main is { IsDisposed: false })
        {
            main.ShowPage(page);
            return;
        }
        main = new MainWindow(() => settings, ApplySettings, Status, page);
        main.FormClosed += (_, _) =>
        {
            main = null;
            ReleaseMemory();
        };
        main.Show();
    }

    [System.Runtime.InteropServices.DllImport("kernel32.dll")]
    static extern bool SetProcessWorkingSetSize(IntPtr process, nint min, nint max);

    /// <summary>Devolve ao Windows a memória usada pela janela, para o Aeonis pesar o mínimo durante o jogo.</summary>
    static void ReleaseMemory()
    {
        System.Runtime.GCSettings.LargeObjectHeapCompactionMode = System.Runtime.GCLargeObjectHeapCompactionMode.CompactOnce;
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        SetProcessWorkingSetSize(Process.GetCurrentProcess().Handle, -1, -1);
    }

    void ApplySettings(Settings updated)
    {
        settings = updated;
        try { settings.Save(); }
        catch (Exception ex) { Notify("Erro ao salvar configurações", ex.Message, ToolTipIcon.Error); }
        Settings.ApplyStartup(settings.StartWithWindows);
        RegisterHotkeys();
        if (Ffmpeg.Locate(settings.FfmpegPath) == null) ShowFfmpegMissing();
        else if (settings.AlwaysOnBuffer && !engine.Running) _ = StartEngineAsync("Desktop", Math.Max(0, settings.MonitorIndex), null);
        UpdateUi();
    }

    void OpenOutputFolder()
    {
        Directory.CreateDirectory(settings.OutputDir);
        Process.Start("explorer.exe", $"\"{settings.OutputDir}\"");
    }

    static void OpenLog()
    {
        if (File.Exists(Paths.LogFile)) Process.Start(new ProcessStartInfo(Paths.LogFile) { UseShellExecute = true });
    }

    void ShowFfmpegMissing()
    {
        Log.Warn("FFmpeg não encontrado.");
        OnUi(() => MessageBox.Show(
            "O Aeonis usa o FFmpeg (6.1 ou mais novo) como motor de captura e ele não foi encontrado.\n\n" +
            "Opção 1: rode scripts\\get-ffmpeg.ps1 do repositório.\n" +
            "Opção 2: coloque ffmpeg.exe ao lado do Aeonis.exe ou em %LOCALAPPDATA%\\Aeonis\\ffmpeg\\.\n" +
            "Opção 3: informe o caminho em Configurações → Caminho do FFmpeg.",
            "Aeonis — FFmpeg não encontrado", MessageBoxButtons.OK, MessageBoxIcon.Warning));
    }

    async Task ExitAsync()
    {
        if (exiting) return;
        exiting = true;
        watcher.Dispose();
        events?.Dispose();
        await highlighter.FlushAsync();
        await StopEngineAndReportAsync();
        highlighter.Dispose();
        hotkeys.Dispose();
        tray.Visible = false;
        tray.Dispose();
        main?.Close();
        invoker.Dispose();
        showSignal.Set();
        ExitThread();
    }
}
