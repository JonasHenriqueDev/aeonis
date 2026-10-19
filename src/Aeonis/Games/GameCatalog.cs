using Aeonis.Core;

namespace Aeonis.Games;

/// <param name="Processes">Nomes de processo sem ".exe".</param>
/// <param name="Provider">Fonte de eventos para destaques automáticos ("lol", "cs2") ou null.</param>
public sealed record GameDef(string Name, string[] Processes, string? Provider = null);

static class GameCatalog
{
    static readonly GameDef[] BuiltIn =
    [
        new("League of Legends", ["League of Legends"], "lol"),
        new("Counter-Strike 2", ["cs2"], "cs2"),
        new("Valorant", ["VALORANT-Win64-Shipping"]),
        new("Fortnite", ["FortniteClient-Win64-Shipping"]),
        new("Apex Legends", ["r5apex", "r5apex_dx12"]),
        new("Overwatch 2", ["Overwatch"]),
        new("Rocket League", ["RocketLeague"]),
        new("Dota 2", ["dota2"]),
        new("Rainbow Six Siege", ["RainbowSix", "RainbowSix_Vulkan"]),
        new("Call of Duty", ["cod"]),
        new("PUBG", ["TslGame"]),
        new("GTA V", ["GTA5", "GTA5_Enhanced"]),
        new("Marvel Rivals", ["Marvel-Win64-Shipping"]),
        new("Deadlock", ["project8"]),
        new("Rust", ["RustClient"]),
        new("Escape from Tarkov", ["EscapeFromTarkov"]),
        new("The Finals", ["Discovery"]),
        new("Elden Ring", ["eldenring"]),
        new("Dead by Daylight", ["DeadByDaylight-Win64-Shipping"]),
        new("Roblox", ["RobloxPlayerBeta"]),
        new("Minecraft", ["Minecraft.Windows"]),
        new("Path of Exile 2", ["PathOfExile", "PathOfExileSteam", "PathOfExile_x64Steam"]),
        new("Brawlhalla", ["Brawlhalla"]),
    ];

    static string? cachedSource;
    static IReadOnlyList<GameDef> cached = BuiltIn;

    public static IReadOnlyList<GameDef> All(Settings s)
    {
        if (s.CustomGames == cachedSource) return cached;
        var list = new List<GameDef>(ParseCustom(s.CustomGames));
        list.AddRange(BuiltIn);
        cachedSource = s.CustomGames;
        return cached = list;
    }

    static IEnumerable<GameDef> ParseCustom(string text)
    {
        foreach (var raw in (text ?? "").Split('\n'))
        {
            var line = raw.Trim();
            if (line.Length == 0 || line.StartsWith('#')) continue;
            var eq = line.IndexOf('=');
            var name = eq > 0 ? line[..eq].Trim() : line;
            var exes = (eq > 0 ? line[(eq + 1)..] : line)
                .Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
                .Select(e => e.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) ? e[..^4] : e)
                .ToArray();
            if (exes.Length > 0) yield return new GameDef(name, exes);
        }
    }
}
