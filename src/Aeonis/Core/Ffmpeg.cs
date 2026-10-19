using System.Diagnostics;
using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;

namespace Aeonis.Core;

/// <summary>Localiza e executa o FFmpeg, que é o motor de captura/encode do Aeonis.</summary>
static class Ffmpeg
{
    public static string? Exe { get; private set; }

    public static string? Locate(string? configured)
    {
        var candidates = new List<string>();
        if (!string.IsNullOrWhiteSpace(configured)) candidates.Add(configured.Trim('"'));
        var baseDir = AppContext.BaseDirectory;
        candidates.Add(Path.Combine(baseDir, "ffmpeg.exe"));
        candidates.Add(Path.Combine(baseDir, "ffmpeg", "ffmpeg.exe"));
        candidates.Add(Path.Combine(baseDir, "ffmpeg", "bin", "ffmpeg.exe"));
        candidates.Add(Path.Combine(Paths.AppData, "ffmpeg", "ffmpeg.exe"));
        foreach (var dir in (Environment.GetEnvironmentVariable("PATH") ?? "").Split(';', StringSplitOptions.RemoveEmptyEntries))
            candidates.Add(Path.Combine(dir.Trim('"'), "ffmpeg.exe"));

        foreach (var c in candidates)
        {
            try { if (File.Exists(c)) return Exe = Path.GetFullPath(c); }
            catch { }
        }
        return Exe = null;
    }

    public static string Q(string path) => "\"" + path + "\"";

    public static ProcessStartInfo StartInfo(string args, bool stdin = false) => new(Exe!, args)
    {
        UseShellExecute = false,
        CreateNoWindow = true,
        RedirectStandardError = true,
        RedirectStandardOutput = true,
        RedirectStandardInput = stdin,
        StandardErrorEncoding = Encoding.UTF8,
    };

    /// <summary>Roda um comando curto (concat, thumbnail, probe) em prioridade baixa para não competir com o jogo.</summary>
    public static async Task<(int Code, string Output)> RunAsync(string args, int timeoutMs = 120_000)
    {
        using var p = new Process { StartInfo = StartInfo(args) };
        var sb = new StringBuilder();
        p.ErrorDataReceived += (_, e) => { if (e.Data != null) lock (sb) sb.AppendLine(e.Data); };
        p.OutputDataReceived += (_, e) => { if (e.Data != null) lock (sb) sb.AppendLine(e.Data); };
        p.Start();
        try { p.PriorityClass = ProcessPriorityClass.BelowNormal; } catch { }
        p.BeginErrorReadLine();
        p.BeginOutputReadLine();

        using var cts = new CancellationTokenSource(timeoutMs);
        try { await p.WaitForExitAsync(cts.Token).ConfigureAwait(false); }
        catch (OperationCanceledException)
        {
            try { p.Kill(true); } catch { }
            lock (sb) return (-1, sb + "\n[timeout]");
        }
        lock (sb) return (p.ExitCode, sb.ToString());
    }

    public static async Task<double> GetDurationAsync(string file)
    {
        var (_, output) = await RunAsync($"-hide_banner -i {Q(file)}", 15_000).ConfigureAwait(false);
        var m = Regex.Match(output, @"Duration:\s*(\d+):(\d+):(\d+(?:\.\d+)?)");
        if (!m.Success) return 0;
        return int.Parse(m.Groups[1].Value) * 3600 + int.Parse(m.Groups[2].Value) * 60
             + double.Parse(m.Groups[3].Value, CultureInfo.InvariantCulture);
    }
}
