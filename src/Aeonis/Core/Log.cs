namespace Aeonis.Core;

static class Log
{
    static readonly object Gate = new();

    public static void Info(string message) => Write("INFO", message);
    public static void Warn(string message) => Write("WARN", message);
    public static void Error(string message, Exception? ex = null) => Write("ERRO", ex == null ? message : $"{message}: {ex}");

    public static void Rotate()
    {
        try
        {
            var fi = new FileInfo(Paths.LogFile);
            if (fi.Exists && fi.Length > 4_000_000) File.Move(fi.FullName, fi.FullName + ".old", true);
        }
        catch { }
    }

    static void Write(string level, string message)
    {
        lock (Gate)
        {
            try
            {
                File.AppendAllText(Paths.LogFile, $"{DateTime.Now:yyyy-MM-dd HH:mm:ss.fff} [{level}] {message}{Environment.NewLine}");
            }
            catch { }
        }
    }
}
