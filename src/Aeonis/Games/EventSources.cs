using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;
using Aeonis.Core;

namespace Aeonis.Games;

/// <summary>Fonte de eventos de jogo que dispara destaques. Usa só APIs oficiais e locais dos próprios jogos.</summary>
interface IEventSource : IDisposable
{
    event Action<string>? Highlight;
    void Start();
}

static class JsonExt
{
    public static JsonElement? Get(this JsonElement e, string name) =>
        e.ValueKind == JsonValueKind.Object && e.TryGetProperty(name, out var v) ? v : null;

    public static string Str(this JsonElement? e) => e is { ValueKind: JsonValueKind.String } v ? v.GetString() ?? "" : "";

    public static int Int(this JsonElement? e) => e is { ValueKind: JsonValueKind.Number } v && v.TryGetInt32(out var i) ? i : 0;
}

/// <summary>
/// League of Legends: Live Client Data API da Riot (https://127.0.0.1:2999), disponível durante a partida.
/// </summary>
sealed class LeagueEventSource(Func<Settings> settings) : IEventSource
{
    public event Action<string>? Highlight;

    // A API usa um certificado autoassinado da Riot; o endereço é fixo em loopback, então aceitamos o certificado.
    readonly HttpClient http = new(new SocketsHttpHandler
    {
        SslOptions = new System.Net.Security.SslClientAuthenticationOptions { RemoteCertificateValidationCallback = (_, _, _, _) => true },
    })
    {
        BaseAddress = new Uri("https://127.0.0.1:2999/liveclientdata/"),
        Timeout = TimeSpan.FromSeconds(2),
    };
    readonly CancellationTokenSource cts = new();

    public void Start() => _ = Task.Run(LoopAsync);

    async Task LoopAsync()
    {
        int lastId = int.MinValue;
        string? me = null;
        while (!cts.IsCancellationRequested)
        {
            try
            {
                me ??= JsonSerializer.Deserialize<string>(await http.GetStringAsync("activeplayername", cts.Token));
                using var doc = JsonDocument.Parse(await http.GetStringAsync("eventdata", cts.Token));
                var events = doc.RootElement.GetProperty("Events");
                int maxId = -1;
                foreach (var e in events.EnumerateArray()) maxId = Math.Max(maxId, e.Get("EventID").Int());

                if (lastId == int.MinValue || maxId < lastId)
                    lastId = maxId; // linha de base: ignora o histórico (Aeonis aberto no meio da partida / nova partida)
                else
                    foreach (var e in events.EnumerateArray())
                    {
                        int id = e.Get("EventID").Int();
                        if (id <= lastId) continue;
                        lastId = id;
                        Handle(e, me ?? "");
                    }
            }
            catch (OperationCanceledException) when (cts.IsCancellationRequested) { break; }
            catch { me = null; } // tela de carregamento ou partida terminando
            try { await Task.Delay(1000, cts.Token); } catch { break; }
        }
    }

    void Handle(JsonElement e, string me)
    {
        bool Me(string n) => n.Length > 0 && me.Length > 0 &&
            (n.Equals(me, StringComparison.OrdinalIgnoreCase) || n.Equals(me.Split('#')[0], StringComparison.OrdinalIgnoreCase));
        var s = settings();
        string? hit = e.Get("EventName").Str() switch
        {
            "ChampionKill" when Me(e.Get("KillerName").Str()) => "Abate",
            "ChampionKill" when s.ClipDeaths && Me(e.Get("VictimName").Str()) => "Morte",
            "ChampionKill" when s.ClipAssists && e.Get("Assisters") is { ValueKind: JsonValueKind.Array } a
                                && a.EnumerateArray().Any(x => Me(x.GetString() ?? "")) => "Assistência",
            "Multikill" when Me(e.Get("KillerName").Str()) => e.Get("KillStreak").Int() switch
            {
                2 => "Double Kill", 3 => "Triple Kill", 4 => "Quadra Kill", _ => "Penta Kill",
            },
            "Ace" when Me(e.Get("Acer").Str()) => "Ace",
            "DragonKill" when Me(e.Get("KillerName").Str()) => "Dragão",
            "BaronKill" when Me(e.Get("KillerName").Str()) => "Barão",
            "HeraldKill" when Me(e.Get("KillerName").Str()) => "Arauto",
            "TurretKilled" when Me(e.Get("KillerName").Str()) => "Torre",
            "InhibKilled" when Me(e.Get("KillerName").Str()) => "Inibidor",
            _ => null,
        };
        if (hit != null) Highlight?.Invoke(hit);
    }

    public void Dispose()
    {
        cts.Cancel();
        http.Dispose();
    }
}

/// <summary>
/// Counter-Strike 2: Game State Integration (recurso oficial da Valve). O Aeonis instala um .cfg na pasta do jogo
/// e recebe o estado via HTTP POST em 127.0.0.1:3005.
/// </summary>
sealed class Cs2EventSource : IEventSource
{
    public const int Port = 3005;
    const string Token = "aeonis-gsi";
    static readonly byte[] Ok = Encoding.ASCII.GetBytes("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");

    public event Action<string>? Highlight;

    readonly Func<Settings> settings;
    readonly CancellationTokenSource cts = new();
    readonly object gate = new();
    TcpListener? listener;
    int? lastKills, lastDeaths;
    string? lastMap;

    public Cs2EventSource(Func<Settings> settings, string? exePath, Action<string, string> notify)
    {
        this.settings = settings;
        EnsureConfig(exePath, notify);
    }

    static void EnsureConfig(string? exePath, Action<string, string> notify)
    {
        try
        {
            // ...\Counter-Strike Global Offensive\game\bin\win64\cs2.exe -> ...\game\csgo\cfg
            var gameDir = exePath == null ? null : Directory.GetParent(exePath)?.Parent?.Parent;
            if (gameDir == null) return;
            var cfgDir = Path.Combine(gameDir.FullName, "csgo", "cfg");
            if (!Directory.Exists(cfgDir)) return;
            var path = Path.Combine(cfgDir, "gamestate_integration_aeonis.cfg");
            if (File.Exists(path)) return;
            File.WriteAllText(path, $$"""
                "Aeonis"
                {
                    "uri" "http://127.0.0.1:{{Port}}/"
                    "timeout" "1.0"
                    "buffer" "0.1"
                    "throttle" "0.25"
                    "heartbeat" "30.0"
                    "auth" { "token" "{{Token}}" }
                    "data"
                    {
                        "provider" "1"
                        "map" "1"
                        "player_id" "1"
                        "player_state" "1"
                        "player_match_stats" "1"
                    }
                }
                """);
            Log.Info($"CS2 GSI instalado em {path}");
            notify("CS2: destaques automáticos ativados", "Reinicie o CS2 uma vez para os abates serem detectados.");
        }
        catch (Exception ex) { Log.Error("Instalação do GSI do CS2", ex); }
    }

    public void Start()
    {
        try
        {
            listener = new TcpListener(IPAddress.Loopback, Port);
            listener.Start();
            _ = Task.Run(AcceptLoopAsync);
        }
        catch (Exception ex) { Log.Error($"Porta {Port} indisponível para o GSI do CS2", ex); }
    }

    async Task AcceptLoopAsync()
    {
        while (!cts.IsCancellationRequested)
        {
            TcpClient client;
            try { client = await listener!.AcceptTcpClientAsync(cts.Token); }
            catch { break; }
            _ = Task.Run(() => HandleClientAsync(client));
        }
    }

    async Task HandleClientAsync(TcpClient client)
    {
        using (client)
        {
            try
            {
                using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cts.Token);
                timeout.CancelAfter(5000);
                var stream = client.GetStream();
                var body = await ReadBodyAsync(stream, timeout.Token);
                await stream.WriteAsync(Ok, timeout.Token);
                if (body != null) Process(body);
            }
            catch { }
        }
    }

    static async Task<string?> ReadBodyAsync(NetworkStream stream, CancellationToken ct)
    {
        var buf = new byte[16384];
        using var ms = new MemoryStream();
        int headerEnd = -1, contentLength = 0;
        while (ms.Length < 2 << 20)
        {
            int n = await stream.ReadAsync(buf, ct);
            if (n == 0) return null;
            ms.Write(buf, 0, n);
            var data = ms.GetBuffer();
            if (headerEnd < 0)
            {
                headerEnd = IndexOf(data, (int)ms.Length, "\r\n\r\n"u8);
                if (headerEnd < 0) continue;
                var head = Encoding.ASCII.GetString(data, 0, headerEnd);
                var m = Regex.Match(head, @"Content-Length:\s*(\d+)", RegexOptions.IgnoreCase);
                contentLength = m.Success ? int.Parse(m.Groups[1].Value) : 0;
            }
            if (ms.Length >= headerEnd + 4 + contentLength)
                return Encoding.UTF8.GetString(data, headerEnd + 4, contentLength);
        }
        return null;
    }

    static int IndexOf(byte[] data, int length, ReadOnlySpan<byte> pattern) =>
        data.AsSpan(0, length).IndexOf(pattern);

    void Process(string body)
    {
        using var doc = JsonDocument.Parse(body);
        var root = doc.RootElement;
        if (root.Get("auth")?.Get("token").Str() != Token) return;

        var player = root.Get("player");
        var map = root.Get("map")?.Get("name").Str();
        lock (gate)
        {
            if (map != lastMap) { lastMap = map; lastKills = lastDeaths = null; }
            // Só o jogador local: quando morto e assistindo um aliado, o "player" é outra pessoa.
            var provider = root.Get("provider")?.Get("steamid").Str();
            if (player == null || string.IsNullOrEmpty(provider) || player?.Get("steamid").Str() != provider) return;
            var stats = player?.Get("match_stats");
            if (stats == null) return;

            int kills = stats.Value.Get("kills").Int();
            int deaths = stats.Value.Get("deaths").Int();
            int roundKills = player?.Get("state")?.Get("round_kills").Int() ?? 0;
            var s = settings();

            if (lastKills is int lk && kills > lk)
                Highlight?.Invoke(roundKills switch
                {
                    >= 5 => "Ace", 4 => "Quadra Kill", 3 => "Triple Kill", 2 => "Double Kill", _ => "Abate",
                });
            if (s.ClipDeaths && lastDeaths is int ld && deaths > ld) Highlight?.Invoke("Morte");
            lastKills = kills;
            lastDeaths = deaths;
        }
    }

    public void Dispose()
    {
        cts.Cancel();
        try { listener?.Stop(); } catch { }
    }
}
