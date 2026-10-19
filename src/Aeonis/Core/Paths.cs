namespace Aeonis.Core;

static class Paths
{
    public static readonly string AppData =
        Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Aeonis");

    public static string SettingsFile => Path.Combine(AppData, "settings.json");
    public static string LogFile => Path.Combine(AppData, "aeonis.log");
    public static string Buffer => Path.Combine(AppData, "buffer");
    public static string Thumbs => Path.Combine(AppData, "thumbs");

    static Paths()
    {
        Directory.CreateDirectory(AppData);
        Directory.CreateDirectory(Buffer);
        Directory.CreateDirectory(Thumbs);
    }
}
