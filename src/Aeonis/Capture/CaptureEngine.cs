using System.Diagnostics;
using System.Text;
using System.Text.Json;
using Aeonis.Core;

namespace Aeonis.Capture;

/// <summary>
/// Um único processo FFmpeg por sessão: captura a tela na GPU (ddagrab/Desktop Duplication), codifica em hardware
/// e grava segmentos MPEG-TS de 2 s em disco. A partir desses segmentos, sem re-encode:
///  - replay buffer: o zelador apaga os segmentos mais antigos que a janela necessária;
///  - clipes: concatena (-c copy) os últimos N segmentos em um MP4;
///  - partida completa: "fixa" um segmento inicial e concatena tudo dali até o fim.
/// Assim um só encoder serve para buffer, clipes e gravação longa ao mesmo tempo.
/// </summary>
sealed class CaptureEngine
{
    public const int SegmentSeconds = 2;
    const string PinFile = "recording.json";

    readonly Func<Settings> settings;
    readonly object lifecycle = new();
    readonly Queue<string> stderrTail = new();

    Process? ffmpeg;
    AudioPipe? systemAudio, micAudio;
    VideoEncoder? encoder;
    System.Threading.Timer? janitor;
    string? segmentDir;
    int? pinStart;
    int busy;
    int restarts;
    int monitor;

    public event Action<string>? Failed;

    public CaptureEngine(Func<Settings> settings) => this.settings = settings;

    public bool Running => segmentDir != null;
    public bool IsRecording => pinStart != null;
    public string Game { get; private set; } = "Desktop";
    public string EncoderLabel => encoder?.Label ?? "-";
    public DateTime? RecordingSince { get; private set; }

    Settings S => settings();

    public async Task StartAsync(string game, int monitorIndex)
    {
        if (Running) return;
        if (Ffmpeg.Exe == null) throw new InvalidOperationException("FFmpeg não encontrado.");
        encoder = await Encoders.ResolveAsync(S.Encoder).ConfigureAwait(false);
        lock (lifecycle)
        {
            if (Running) return;
            Game = game;
            monitor = monitorIndex;
            restarts = 0;
            segmentDir = Path.Combine(Paths.Buffer, DateTime.Now.ToString("yyyyMMdd_HHmmss_fff"));
            Directory.CreateDirectory(segmentDir);
            Launch(0);
            janitor = new System.Threading.Timer(_ => Janitor(), null, 2000, 2000);
        }
        Log.Info($"Captura iniciada: {game} (monitor {monitorIndex}, {encoder.Label}, {S.Fps} fps, {S.BitrateMbps} Mbps)");
    }

    void Launch(int firstSegment)
    {
        var s = S;
        systemAudio = s.SystemAudio ? AudioPipe.TryCreate(true, null, "sys") : null;
        micAudio = s.Microphone ? AudioPipe.TryCreate(false, s.MicDevice, "mic") : null;
        var pipes = new[] { systemAudio, micAudio }.OfType<AudioPipe>().ToList();
        var enc = encoder!;

        var args = new StringBuilder("-hide_banner -loglevel warning -nostats -y ");
        foreach (var p in pipes) args.Append(p.InputArgs()).Append(' ');

        var graph = new StringBuilder(
            $"ddagrab=output_idx={monitor}:framerate={s.Fps}:draw_mouse={(s.DrawMouse ? 1 : 0)}{enc.FilterSuffix}[v]");
        for (int i = 0; i < pipes.Count; i++)
            graph.Append($";[{i}:a]aresample=48000,aformat=sample_fmts=fltp:channel_layouts=stereo[a{i}]");
        if (pipes.Count == 2) graph.Append(";[a0][a1]amix=inputs=2:duration=longest:normalize=0[a]");

        args.Append($"-filter_complex \"{graph}\" -map \"[v]\" ");
        if (pipes.Count == 1) args.Append("-map \"[a0]\" ");
        else if (pipes.Count == 2) args.Append("-map \"[a]\" ");

        args.Append(Encoders.Args(enc, s.BitrateMbps, s.Fps * SegmentSeconds)).Append(' ');
        // Keyframe exatamente em cada fronteira de segmento: cortes e concatenação sem re-encode.
        args.Append($"-force_key_frames \"expr:gte(t,n_forced*{SegmentSeconds})\" ");
        if (pipes.Count > 0) args.Append("-c:a aac -b:a 160k ");
        args.Append($"-f segment -segment_time {SegmentSeconds} -segment_format mpegts -segment_start_number {firstSegment} ");
        args.Append(Ffmpeg.Q(Path.Combine(segmentDir!, "seg_%06d.ts")));

        Log.Info("ffmpeg " + args);
        var proc = new Process { StartInfo = Ffmpeg.StartInfo(args.ToString(), stdin: true) };
        proc.ErrorDataReceived += (_, e) =>
        {
            if (e.Data == null) return;
            lock (stderrTail)
            {
                stderrTail.Enqueue(e.Data);
                while (stderrTail.Count > 40) stderrTail.Dequeue();
            }
        };
        proc.OutputDataReceived += (_, _) => { };
        proc.Start();
        proc.BeginErrorReadLine();
        proc.BeginOutputReadLine();
        // Acima do normal para não perder frames quando o jogo satura a CPU; o trabalho pesado está na GPU.
        try { proc.PriorityClass = ProcessPriorityClass.AboveNormal; } catch { }
        foreach (var p in pipes) p.Start();
        ffmpeg = proc;
    }

    void Janitor()
    {
        if (!Monitor.TryEnter(lifecycle)) return;
        try
        {
            if (segmentDir == null) return;
            if (ffmpeg is { HasExited: true }) { HandleCrash(); return; }
            if (Volatile.Read(ref busy) > 0) return;

            var segments = ListSegments();
            if (segments.Count == 0) return;
            var s = S;
            int keepSeconds = Math.Max(s.ReplaySeconds, s.ClipPreSeconds + s.ClipPostSeconds + Highlighter.MaxExtendSeconds) + 10;
            int keepFrom = segments[^1].Index - keepSeconds / SegmentSeconds;
            if (pinStart is int pin) keepFrom = Math.Min(keepFrom, pin);
            foreach (var seg in segments)
            {
                if (seg.Index >= keepFrom) break;
                try { File.Delete(seg.Path); } catch { }
            }
        }
        catch (Exception ex) { Log.Error("Zelador do buffer", ex); }
        finally { Monitor.Exit(lifecycle); }
    }

    void HandleCrash()
    {
        string tail;
        lock (stderrTail) tail = string.Join(Environment.NewLine, stderrTail);
        Log.Error($"FFmpeg encerrou inesperadamente (código {ffmpeg?.ExitCode}):{Environment.NewLine}{tail}");
        DisposeProcess();
        if (restarts++ < 3)
        {
            var segments = ListSegments();
            Launch(segments.Count > 0 ? segments[^1].Index + 1 : 0);
            return;
        }
        janitor?.Dispose();
        janitor = null;
        var lastLine = tail.Split('\n', StringSplitOptions.RemoveEmptyEntries).LastOrDefault()?.Trim() ?? "erro desconhecido";
        Failed?.Invoke($"A captura parou: {lastLine}");
    }

    public async Task<string?> StopAsync()
    {
        string? recording = null;
        string? dir;
        lock (lifecycle)
        {
            if (segmentDir == null) return null;
            janitor?.Dispose();
            janitor = null;
            DisposeProcess();
            dir = segmentDir;
        }
        if (pinStart != null) recording = await EndRecordingAsync().ConfigureAwait(false);
        lock (lifecycle)
        {
            segmentDir = null;
            try { Directory.Delete(dir, true); } catch (Exception ex) { Log.Warn($"Não foi possível limpar {dir}: {ex.Message}"); }
        }
        Log.Info("Captura encerrada.");
        return recording;
    }

    void DisposeProcess()
    {
        var p = ffmpeg;
        ffmpeg = null;
        if (p != null)
        {
            try
            {
                if (!p.HasExited)
                {
                    p.StandardInput.Write('q');
                    p.StandardInput.Flush();
                    if (!p.WaitForExit(8000)) p.Kill(true);
                }
            }
            catch { try { p.Kill(true); } catch { } }
            p.Dispose();
        }
        systemAudio?.Dispose();
        micAudio?.Dispose();
        systemAudio = micAudio = null;
    }

    public async Task<string?> SaveClipAsync(int seconds, string label)
    {
        List<Segment> pick;
        string game;
        lock (lifecycle)
        {
            if (segmentDir == null) return null;
            Interlocked.Increment(ref busy);
            var segments = ListSegments();
            int need = (int)Math.Ceiling(seconds / (double)SegmentSeconds) + 1;
            pick = segments.Skip(Math.Max(0, segments.Count - need)).ToList();
            game = Game;
        }
        try
        {
            if (pick.Count == 0) return null;
            return await ConcatAsync(pick, Clips.NewPath(S.OutputDir, game, label)).ConfigureAwait(false);
        }
        finally { Interlocked.Decrement(ref busy); }
    }

    public void BeginRecording()
    {
        lock (lifecycle)
        {
            if (segmentDir == null || pinStart != null) return;
            var segments = ListSegments();
            pinStart = segments.Count > 0 ? segments[^1].Index : 0;
            RecordingSince = DateTime.Now;
            File.WriteAllText(Path.Combine(segmentDir, PinFile),
                JsonSerializer.Serialize(new PinInfo(Game, pinStart.Value, S.OutputDir)));
        }
        Log.Info($"Gravação longa iniciada a partir do segmento {pinStart}.");
    }

    public async Task<string?> EndRecordingAsync()
    {
        List<Segment> pick;
        string game, dir;
        lock (lifecycle)
        {
            if (segmentDir == null || pinStart is not int pin) return null;
            Interlocked.Increment(ref busy);
            dir = segmentDir;
            pick = ListSegments().Where(s => s.Index >= pin).ToList();
            game = Game;
        }
        try
        {
            var label = Game == "Desktop" ? "Gravação" : "Partida";
            var result = pick.Count > 0 ? await ConcatAsync(pick, Clips.NewPath(S.OutputDir, game, label)).ConfigureAwait(false) : null;
            return result;
        }
        finally
        {
            lock (lifecycle)
            {
                pinStart = null;
                RecordingSince = null;
                try { File.Delete(Path.Combine(dir, PinFile)); } catch { }
            }
            Interlocked.Decrement(ref busy);
        }
    }

    async Task<string?> ConcatAsync(List<Segment> segments, string output)
    {
        var list = new StringBuilder();
        foreach (var seg in segments)
            list.Append("file '").Append(seg.Path.Replace('\\', '/').Replace("'", "'\\''")).Append("'\n");
        var listPath = Path.Combine(Path.GetDirectoryName(segments[0].Path)!, $"list_{Guid.NewGuid():N}.txt");
        await File.WriteAllTextAsync(listPath, list.ToString()).ConfigureAwait(false);
        try
        {
            var tag = encoder?.IsHevc == true ? "-tag:v hvc1 " : "";
            var (code, output_) = await Ffmpeg.RunAsync(
                $"-hide_banner -loglevel error -f concat -safe 0 -i {Ffmpeg.Q(listPath)} -map 0 -c copy {tag}-bsf:a aac_adtstoasc -movflags +faststart -y {Ffmpeg.Q(output)}",
                600_000).ConfigureAwait(false);
            if (code != 0 || !File.Exists(output) || new FileInfo(output).Length == 0)
            {
                Log.Error($"Falha ao gerar {output}: {output_}");
                try { File.Delete(output); } catch { }
                return null;
            }
            Log.Info($"Salvo: {output} ({segments.Count} segmentos)");
            return output;
        }
        finally { try { File.Delete(listPath); } catch { } }
    }

    List<Segment> ListSegments() => segmentDir == null ? [] : ListSegments(segmentDir);

    static List<Segment> ListSegments(string dir)
    {
        var result = new List<Segment>();
        foreach (var path in Directory.EnumerateFiles(dir, "seg_*.ts"))
        {
            var name = Path.GetFileNameWithoutExtension(path);
            if (int.TryParse(name.AsSpan(4), out int idx)) result.Add(new Segment(idx, path));
        }
        result.Sort((a, b) => a.Index.CompareTo(b.Index));
        return result;
    }

    /// <summary>Recupera gravações longas interrompidas (queda de energia, crash) e limpa buffers órfãos.</summary>
    public static async Task RecoverLeftoversAsync()
    {
        foreach (var dir in Directory.EnumerateDirectories(Paths.Buffer).ToList())
        {
            try
            {
                var pinPath = Path.Combine(dir, PinFile);
                if (File.Exists(pinPath) && JsonSerializer.Deserialize<PinInfo>(File.ReadAllText(pinPath)) is { } pin)
                {
                    var segs = ListSegments(dir).Where(s => s.Index >= pin.Start).ToList();
                    if (segs.Count > 0)
                    {
                        var engine = new CaptureEngine(() => new Settings());
                        var output = await engine.ConcatAsync(segs, Clips.NewPath(pin.OutputDir, pin.Game, "Recuperado")).ConfigureAwait(false);
                        if (output == null) continue; // mantém os segmentos para nova tentativa
                    }
                }
                Directory.Delete(dir, true);
            }
            catch (Exception ex) { Log.Warn($"Limpeza de {dir}: {ex.Message}"); }
        }
    }

    readonly record struct Segment(int Index, string Path);
    sealed record PinInfo(string Game, int Start, string OutputDir);
}
