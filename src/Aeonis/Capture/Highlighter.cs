using Aeonis.Core;

namespace Aeonis.Capture;

/// <summary>
/// Junta eventos próximos (ex.: abate + double kill + triple kill) em um único clipe: cada evento empurra o fim
/// do clipe para "agora + segundos depois", limitado a <see cref="MaxExtendSeconds"/> desde o primeiro evento.
/// </summary>
sealed class Highlighter : IDisposable
{
    public const int MaxExtendSeconds = 60;

    static readonly string[] Priority =
    [
        "Penta Kill", "Ace", "Quadra Kill", "Triple Kill", "Double Kill",
        "Barão", "Dragão", "Arauto", "Inibidor", "Torre", "Abate", "Assistência", "Morte",
    ];

    readonly Func<Settings> settings;
    readonly Func<int, string, Task> save;
    readonly object gate = new();
    readonly System.Threading.Timer timer;
    readonly List<string> reasons = [];
    DateTime? first;
    DateTime deadline;

    public Highlighter(Func<Settings> settings, Func<int, string, Task> save)
    {
        this.settings = settings;
        this.save = save;
        timer = new System.Threading.Timer(_ => _ = FireAsync());
    }

    public void Trigger(string reason)
    {
        lock (gate)
        {
            var now = DateTime.UtcNow;
            first ??= now;
            reasons.Add(reason);
            deadline = now.AddSeconds(settings().ClipPostSeconds);
            var max = first.Value.AddSeconds(MaxExtendSeconds);
            if (deadline > max) deadline = max;
            timer.Change(Math.Max(0, (int)(deadline - now).TotalMilliseconds), Timeout.Infinite);
        }
        Log.Info($"Destaque: {reason}");
    }

    /// <summary>Salva imediatamente um destaque pendente (ex.: o jogo fechou antes do fim da janela).</summary>
    public Task FlushAsync()
    {
        lock (gate) timer.Change(Timeout.Infinite, Timeout.Infinite);
        return FireAsync();
    }

    async Task FireAsync()
    {
        int seconds;
        string label;
        lock (gate)
        {
            if (first == null) return;
            var end = DateTime.UtcNow < deadline ? DateTime.UtcNow : deadline;
            seconds = settings().ClipPreSeconds + (int)Math.Ceiling((end - first.Value).TotalSeconds);
            label = Label(reasons);
            first = null;
            reasons.Clear();
        }
        try { await save(Math.Max(seconds, 3), label).ConfigureAwait(false); }
        catch (Exception ex) { Log.Error("Falha ao salvar destaque", ex); }
    }

    static string Label(List<string> r)
    {
        foreach (var p in Priority)
        {
            if (!r.Contains(p)) continue;
            int kills = r.Count(x => x == "Abate");
            return p == "Abate" && kills > 1 ? $"{kills} Abates" : p;
        }
        return r.Count > 0 ? r[0] : "Destaque";
    }

    public void Dispose() => timer.Dispose();
}
