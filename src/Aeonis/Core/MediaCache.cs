using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.RegularExpressions;

namespace Aeonis.Core;

/// <summary>Miniaturas e durações dos vídeos, cacheadas em %LOCALAPPDATA%\Aeonis\thumbs (um único FFmpeg por vídeo).</summary>
static class MediaCache
{
    public static string Key(FileInfo f) => Convert.ToHexString(SHA1.HashData(Encoding.UTF8.GetBytes(
        $"{f.FullName}|{f.Length}|{f.LastWriteTimeUtc.Ticks}")))[..20].ToLowerInvariant();

    static string Jpg(string key) => Path.Combine(Paths.Thumbs, key + ".jpg");
    static string Dur(string key) => Path.Combine(Paths.Thumbs, key + ".dur");

    public static bool HasThumb(string key) => File.Exists(Jpg(key));

    public static double? Duration(string key)
    {
        try
        {
            return File.Exists(Dur(key)) && double.TryParse(File.ReadAllText(Dur(key)), NumberStyles.Float, CultureInfo.InvariantCulture, out var d)
                ? d : null;
        }
        catch { return null; }
    }

    /// <summary>Gera a miniatura e descobre a duração na mesma execução do FFmpeg.</summary>
    public static async Task<double?> GenerateAsync(string video, string key)
    {
        var jpg = Jpg(key);
        var (_, output) = await Ffmpeg.RunAsync(
            $"-hide_banner -ss 2 -i {Ffmpeg.Q(video)} -frames:v 1 -vf scale=480:-2 -q:v 4 -y {Ffmpeg.Q(jpg)}", 30_000).ConfigureAwait(false);
        if (!File.Exists(jpg))
            (_, output) = await Ffmpeg.RunAsync(
                $"-hide_banner -i {Ffmpeg.Q(video)} -frames:v 1 -vf scale=480:-2 -q:v 4 -y {Ffmpeg.Q(jpg)}", 30_000).ConfigureAwait(false);

        var m = Regex.Match(output, @"Duration:\s*(\d+):(\d+):(\d+(?:\.\d+)?)");
        if (!m.Success) return null;
        double d = int.Parse(m.Groups[1].Value) * 3600 + int.Parse(m.Groups[2].Value) * 60
                   + double.Parse(m.Groups[3].Value, CultureInfo.InvariantCulture);
        try { await File.WriteAllTextAsync(Dur(key), d.ToString(CultureInfo.InvariantCulture)).ConfigureAwait(false); } catch { }
        return d;
    }

    /// <summary>Salva o ícone do executável do jogo para a barra lateral da biblioteca.</summary>
    public static void SaveGameIcon(string game, string? exe)
    {
        if (exe == null || !File.Exists(exe)) return;
        var png = Path.Combine(Paths.Icons, Capture.Clips.Sanitize(game) + ".png");
        if (File.Exists(png)) return;
        try
        {
            using var icon = Icon.ExtractIcon(exe, 0, 64) ?? Icon.ExtractAssociatedIcon(exe);
            if (icon == null) return;
            using var bmp = icon.ToBitmap();
            bmp.Save(png, System.Drawing.Imaging.ImageFormat.Png);
        }
        catch (Exception ex) { Log.Warn($"Ícone de {game}: {ex.Message}"); }
    }
}
