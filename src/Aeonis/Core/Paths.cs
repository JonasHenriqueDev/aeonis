namespace Aeonis.Core;

static class Paths
{
    /// <summary>Dados do app. AEONIS_DATA permite outra pasta (testes, modo portátil).</summary>
    public static readonly string AppData =
        Environment.GetEnvironmentVariable("AEONIS_DATA") is { Length: > 0 } custom
            ? Path.GetFullPath(custom)
            : Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Aeonis");

    public static string SettingsFile => Path.Combine(AppData, "settings.json");
    public static string LogFile => Path.Combine(AppData, "aeonis.log");
    public static string Buffer => Path.Combine(AppData, "buffer");
    public static string Thumbs => Path.Combine(AppData, "thumbs");
    public static string Icons => Path.Combine(AppData, "icons");
    public static string LibraryFile => Path.Combine(AppData, "library.jsonl");

    static Paths()
    {
        Directory.CreateDirectory(AppData);
        Directory.CreateDirectory(Buffer);
        Directory.CreateDirectory(Thumbs);
        Directory.CreateDirectory(Icons);
    }
}
