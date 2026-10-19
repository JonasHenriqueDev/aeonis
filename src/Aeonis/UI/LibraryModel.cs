using System.Text.RegularExpressions;
using Aeonis.Capture;
using Aeonis.Core;

namespace Aeonis.UI;

sealed record MarkerDto(double T, string Label);

sealed record ClipDto(
    string Id, string Path, string Url, string Name, string Kind, string Label, string Game,
    DateTime Created, double? Duration, long Size, string? Thumb, List<MarkerDto> Markers);

sealed record MatchDto(string Id, string Game, DateTime Start, DateTime End, List<ClipDto> Clips);

sealed record GameDto(string Name, string? Icon, int Clips);

sealed record LibraryDto(List<MatchDto> Matches, List<GameDto> Games, long TotalBytes, string OutputDir);

/// <summary>
/// Monta a biblioteca no formato do Outplayed: dia → partida → clipes. A partida vem do histórico
/// (<see cref="Library"/>); vídeos sem histórico (antigos, importados) são agrupados por jogo e proximidade no tempo.
/// </summary>
static class LibraryModel
{
    static readonly TimeSpan SessionGap = TimeSpan.FromMinutes(45);
    static readonly Regex NamePattern = new(@"_\d{4}-\d{2}-\d{2}_\d{2}-\d{2}-\d{2}_(.+?)(?:_\d+)?$");

    public static string MediaUrl(string outputDir, string file) =>
        "https://media.aeonis/" + string.Join('/',
            Path.GetRelativePath(outputDir, file).Split(Path.DirectorySeparatorChar).Select(Uri.EscapeDataString));

    public static string ThumbUrl(string key) => $"https://thumbs.aeonis/{key}.jpg";

    public static LibraryDto Build(string outputDir)
    {
        var entries = Library.Load();
        var sessions = new Dictionary<string, LibEntry>();
        var ends = new Dictionary<string, DateTime>();
        var clipMeta = new Dictionary<string, LibEntry>(StringComparer.OrdinalIgnoreCase);
        var events = new Dictionary<string, List<LibEntry>>();
        foreach (var e in entries)
        {
            switch (e.T)
            {
                case "session": sessions[e.S] = e; break;
                case "end": ends[e.S] = e.At; break;
                case "clip" when e.Path != null: clipMeta[e.Path] = e; break;
                case "event":
                    if (!events.TryGetValue(e.S, out var list)) events[e.S] = list = [];
                    list.Add(e);
                    break;
            }
        }

        var clips = new List<(ClipDto Clip, string? Session)>();
        long total = 0;
        if (Directory.Exists(outputDir))
        {
            foreach (var f in new DirectoryInfo(outputDir).EnumerateFiles("*.mp4", SearchOption.AllDirectories))
            {
                total += f.Length;
                clipMeta.TryGetValue(f.FullName, out var meta);
                var session = string.IsNullOrEmpty(meta?.S) ? null : meta!.S;
                var game = (session != null && sessions.TryGetValue(session, out var se) ? se.Game : null)
                           ?? f.Directory?.Name ?? "Outros";
                var name = Path.GetFileNameWithoutExtension(f.Name);
                var label = meta?.Label ?? LabelFromName(name);
                var kind = meta?.Kind ?? KindFromLabel(label);
                var key = MediaCache.Key(f);
                var duration = meta?.Duration ?? MediaCache.Duration(key);

                var markers = new List<MarkerDto>();
                if (session != null && meta?.Start is { } start && duration is { } d && events.TryGetValue(session, out var evs))
                    foreach (var ev in evs)
                    {
                        var t = (ev.At - start).TotalSeconds;
                        if (t >= 0 && t <= d) markers.Add(new MarkerDto(Math.Round(t, 1), ev.Label ?? ""));
                    }

                clips.Add((new ClipDto(key, f.FullName, MediaUrl(outputDir, f.FullName), name, kind, label, game,
                    meta?.At ?? f.LastWriteTime, duration, f.Length,
                    MediaCache.HasThumb(key) ? ThumbUrl(key) : null, markers), session));
            }
        }

        var matches = new List<MatchDto>();
        foreach (var g in clips.Where(c => c.Session != null).GroupBy(c => c.Session!))
        {
            var list = g.Select(c => c.Clip).ToList();
            sessions.TryGetValue(g.Key, out var se);
            var start = se?.At ?? list.Min(c => c.Created.AddSeconds(-(c.Duration ?? 0)));
            var end = ends.TryGetValue(g.Key, out var en) ? en : list.Max(c => c.Created);
            matches.Add(new MatchDto(g.Key, se?.Game ?? list[0].Game, start, end, Order(list)));
        }

        // Sem histórico: agrupa por jogo, abrindo uma nova "partida" quando há um intervalo grande entre vídeos.
        foreach (var byGame in clips.Where(c => c.Session == null).Select(c => c.Clip).GroupBy(c => c.Game))
        {
            var current = new List<ClipDto>();
            foreach (var c in byGame.OrderBy(c => c.Created))
            {
                if (current.Count > 0 && c.Created - current[^1].Created > SessionGap)
                {
                    matches.Add(Synthetic(byGame.Key, current));
                    current = [];
                }
                current.Add(c);
            }
            if (current.Count > 0) matches.Add(Synthetic(byGame.Key, current));
        }

        matches.Sort((a, b) => b.Start.CompareTo(a.Start));
        var games = matches.GroupBy(m => m.Game)
            .Select(g => new GameDto(g.Key, IconUrl(g.Key), g.Sum(m => m.Clips.Count)))
            .OrderByDescending(g => matches.First(m => m.Game == g.Name).Start)
            .ToList();
        return new LibraryDto(matches, games, total, outputDir);
    }

    static MatchDto Synthetic(string game, List<ClipDto> clips) =>
        new("auto-" + clips[0].Id, game, clips.Min(c => c.Created.AddSeconds(-(c.Duration ?? 0))), clips.Max(c => c.Created), Order(clips));

    static List<ClipDto> Order(List<ClipDto> clips) =>
        clips.OrderBy(c => c.Kind == "full" ? 0 : 1).ThenBy(c => c.Created).ToList();

    static string? IconUrl(string game)
    {
        var file = Clips.Sanitize(game) + ".png";
        return File.Exists(Path.Combine(Paths.Icons, file)) ? "https://icons.aeonis/" + Uri.EscapeDataString(file) : null;
    }

    static string LabelFromName(string name)
    {
        var m = NamePattern.Match(name);
        return m.Success ? m.Groups[1].Value : name;
    }

    static string KindFromLabel(string label) => label switch
    {
        "Partida" or "Gravação" or "Recuperado" => "full",
        "Clipe" or "Teste" => "manual",
        _ when label.EndsWith("(corte)") => "manual",
        _ => "highlight",
    };
}
