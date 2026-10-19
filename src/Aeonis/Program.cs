using Aeonis.Capture;
using Aeonis.Core;
using Aeonis.UI;

namespace Aeonis;

static class Program
{
    [STAThread]
    static int Main(string[] args)
    {
        Log.Rotate();
        if (args.Contains("--selftest")) return SelfTest(args);

        using var mutex = new Mutex(true, "Aeonis.SingleInstance", out bool first);
        if (!first)
        {
            MessageBox.Show("O Aeonis já está em execução (veja o ícone na bandeja do sistema).", "Aeonis");
            return 0;
        }

        ApplicationConfiguration.Initialize();
        Application.ThreadException += (_, e) => Log.Error("Exceção na UI", e.Exception);
        AppDomain.CurrentDomain.UnhandledException += (_, e) => Log.Error("Exceção não tratada", e.ExceptionObject as Exception);
        TaskScheduler.UnobservedTaskException += (_, e) => { Log.Error("Task não observada", e.Exception); e.SetObserved(); };
        Application.Run(new TrayApp(args.Contains("--minimized")));
        return 0;
    }

    static string? Arg(string[] args, string name)
    {
        int i = Array.IndexOf(args, name);
        return i >= 0 && i + 1 < args.Length ? args[i + 1] : null;
    }

    /// <summary>
    /// Teste de ponta a ponta sem UI: aeonis --selftest [--ffmpeg caminho] [--out pasta] [--seconds N]
    /// Grava, salva um clipe, faz uma gravação longa e escreve o resultado em %LOCALAPPDATA%\Aeonis\selftest.txt.
    /// </summary>
    static int SelfTest(string[] args)
    {
        var report = new List<string>();
        int code = 1;
        try
        {
            var s = new Settings();
            if (Arg(args, "--out") is { } outDir) s.OutputDir = outDir;
            if (Arg(args, "--encoder") is { } enc) s.Encoder = Enum.Parse<EncoderChoice>(enc, true);
            if (args.Contains("--no-audio")) s.SystemAudio = false;
            if (args.Contains("--mic")) s.Microphone = true;
            int seconds = int.TryParse(Arg(args, "--seconds"), out var n) ? n : 12;

            if (Ffmpeg.Locate(Arg(args, "--ffmpeg")) == null) throw new Exception("FFmpeg não encontrado");
            report.Add($"ffmpeg: {Ffmpeg.Exe}");

            var engine = new CaptureEngine(() => s);
            engine.Failed += m => report.Add("FALHA: " + m);
            engine.StartAsync("SelfTest", 0).GetAwaiter().GetResult();
            report.Add($"encoder: {engine.EncoderLabel}");
            Thread.Sleep(2000);
            engine.BeginRecording();
            Thread.Sleep(seconds * 1000);
            var clip = engine.SaveClipAsync(6, "Teste").GetAwaiter().GetResult();
            report.Add($"clipe: {clip ?? "(falhou)"}");
            var rec = engine.StopAsync().GetAwaiter().GetResult();
            report.Add($"gravação: {rec ?? "(falhou)"}");
            foreach (var f in new[] { clip, rec })
                if (f != null) report.Add($"  {Path.GetFileName(f)}: {new FileInfo(f).Length / 1024} KiB, {Ffmpeg.GetDurationAsync(f).GetAwaiter().GetResult():0.0}s");
            code = clip != null && rec != null ? 0 : 1;
        }
        catch (Exception ex) { report.Add("ERRO: " + ex); }
        report.Add(code == 0 ? "OK" : "FALHOU");
        File.WriteAllLines(Path.Combine(Paths.AppData, "selftest.txt"), report);
        return code;
    }
}
