using Aeonis.Core;

namespace Aeonis.Capture;

static class Clips
{
    public static string Sanitize(string s)
    {
        var invalid = Path.GetInvalidFileNameChars();
        var chars = s.Select(c => invalid.Contains(c) ? '_' : c).ToArray();
        var r = new string(chars).Trim().TrimEnd('.');
        return r.Length == 0 ? "Clip" : r;
    }

    public static string NewPath(string outputDir, string game, string label)
    {
        var dir = Path.Combine(outputDir, Sanitize(game));
        Directory.CreateDirectory(dir);
        var baseName = $"{Sanitize(game)}_{DateTime.Now:yyyy-MM-dd_HH-mm-ss}_{Sanitize(label)}";
        var path = Path.Combine(dir, baseName + ".mp4");
        for (int i = 2; File.Exists(path); i++) path = Path.Combine(dir, $"{baseName}_{i}.mp4");
        return path;
    }

    /// <summary>Apaga os vídeos mais antigos até a pasta ficar abaixo do limite configurado.</summary>
    public static void EnforceQuota(string outputDir, double maxGb)
    {
        if (maxGb <= 0 || !Directory.Exists(outputDir)) return;
        try
        {
            long limit = (long)(maxGb * 1024 * 1024 * 1024);
            var files = new DirectoryInfo(outputDir).EnumerateFiles("*.mp4", SearchOption.AllDirectories)
                .OrderBy(f => f.LastWriteTimeUtc).ToList();
            long total = files.Sum(f => f.Length);
            foreach (var f in files)
            {
                if (total <= limit) break;
                try
                {
                    total -= f.Length;
                    f.Delete();
                    Log.Info($"Limite de armazenamento: removido {f.FullName}");
                }
                catch { }
            }
        }
        catch (Exception ex) { Log.Error("Limite de armazenamento", ex); }
    }
}
