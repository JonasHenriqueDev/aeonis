using System.Text.Json;
using System.Text.Json.Serialization;

namespace Aeonis.Core;

/// <summary>Uma linha do histórico: início/fim de sessão (partida), clipe salvo ou evento de destaque.</summary>
public sealed class LibEntry
{
    public string T { get; set; } = "";      // session | end | clip | event
    public string S { get; set; } = "";      // id da sessão
    public DateTime At { get; set; }
    public string? Game { get; set; }
    public string? Exe { get; set; }
    public string? Path { get; set; }
    public string? Kind { get; set; }        // manual | highlight | full
    public string? Label { get; set; }
    public double? Duration { get; set; }
    public DateTime? Start { get; set; }     // instante do primeiro frame do clipe
}

/// <summary>
/// Histórico append-only em %LOCALAPPDATA%\Aeonis\library.jsonl. Liga cada vídeo à partida (sessão) e ao jogo
/// de onde veio e guarda os eventos para marcar a linha do tempo. Nada é gravado na pasta de vídeos.
/// </summary>
static class Library
{
    static readonly object Gate = new();
    static readonly JsonSerializerOptions Json = new() { DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull };

    public static void SessionStarted(string session, string game, string? exe) =>
        Append(new LibEntry { T = "session", S = session, At = DateTime.Now, Game = game, Exe = exe });

    public static void SessionEnded(string session) =>
        Append(new LibEntry { T = "end", S = session, At = DateTime.Now });

    public static void Event(string session, string label) =>
        Append(new LibEntry { T = "event", S = session, At = DateTime.Now, Label = label });

    /// <param name="requestedAt">Momento em que o clipe foi pedido (fim do vídeo).</param>
    /// <param name="start">Início conhecido (gravação longa); senão é deduzido de requestedAt − duração.</param>
    public static async Task ClipSavedAsync(string session, string path, string kind, string label, DateTime requestedAt, DateTime? start = null)
    {
        double duration = await Ffmpeg.GetDurationAsync(path).ConfigureAwait(false);
        Append(new LibEntry
        {
            T = "clip", S = session, At = requestedAt, Path = path, Kind = kind, Label = label,
            Duration = duration > 0 ? duration : null,
            Start = start ?? (duration > 0 ? requestedAt.AddSeconds(-duration) : null),
        });
    }

    /// <summary>Registra um corte como pertencente à mesma partida do vídeo original.</summary>
    public static async Task DerivedAsync(string source, string output, double offsetSeconds)
    {
        var src = Load().LastOrDefault(e => e.T == "clip" && string.Equals(e.Path, source, StringComparison.OrdinalIgnoreCase));
        double duration = await Ffmpeg.GetDurationAsync(output).ConfigureAwait(false);
        Append(new LibEntry
        {
            T = "clip", S = src?.S ?? "", At = DateTime.Now, Path = output,
            Kind = src?.Kind == "full" ? "manual" : src?.Kind ?? "manual",
            Label = (src?.Label ?? "Clipe") + " (corte)",
            Duration = duration > 0 ? duration : null,
            Start = src?.Start?.AddSeconds(offsetSeconds),
        });
    }

    public static List<LibEntry> Load()
    {
        var list = new List<LibEntry>();
        lock (Gate)
        {
            if (!File.Exists(Paths.LibraryFile)) return list;
            foreach (var line in File.ReadLines(Paths.LibraryFile))
            {
                if (line.Length == 0) continue;
                try { if (JsonSerializer.Deserialize<LibEntry>(line, Json) is { } e) list.Add(e); }
                catch { }
            }
        }
        return list;
    }

    static void Append(LibEntry e)
    {
        lock (Gate)
        {
            try { File.AppendAllText(Paths.LibraryFile, JsonSerializer.Serialize(e, Json) + "\n"); }
            catch (Exception ex) { Log.Error("Falha ao gravar histórico", ex); }
        }
    }
}
