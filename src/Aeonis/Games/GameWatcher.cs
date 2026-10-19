using System.Diagnostics;
using Aeonis.Core;

namespace Aeonis.Games;

/// <summary>
/// Detecta jogos por nome de processo (uma enumeração a cada 3 s enquanto nenhum jogo roda; depois só verifica o PID).
/// Espera a janela do jogo aparecer para descobrir em qual monitor ele está.
/// </summary>
sealed class GameWatcher : IDisposable
{
    public event Action<GameDef, int, string?>? GameStarted; // jogo, monitor, caminho do exe
    public event Action<GameDef>? GameStopped;

    readonly Func<IReadOnlyList<GameDef>> games;
    readonly System.Windows.Forms.Timer timer = new() { Interval = 3000 };
    GameDef? current, pending;
    int currentPid, pendingPid;
    string currentName = "";
    DateTime pendingSince;

    public GameWatcher(Func<IReadOnlyList<GameDef>> games)
    {
        this.games = games;
        timer.Tick += (_, _) => Tick();
    }

    public GameDef? Current => current;

    public void Start()
    {
        timer.Start();
        Tick();
    }

    void Tick()
    {
        try
        {
            if (current != null)
            {
                if (!IsAlive(currentPid, currentName))
                {
                    var g = current;
                    current = null;
                    Log.Info($"Jogo encerrado: {g.Name}");
                    GameStopped?.Invoke(g);
                }
                return;
            }

            if (pending != null)
            {
                Process? p = null;
                try { p = Process.GetProcessById(pendingPid); } catch { }
                if (p == null) { pending = null; return; }
                using (p)
                {
                    IntPtr hwnd = IntPtr.Zero;
                    try { hwnd = p.MainWindowHandle; } catch { }
                    if (hwnd == IntPtr.Zero && DateTime.UtcNow - pendingSince < TimeSpan.FromSeconds(20)) return;

                    string? exe = null;
                    try { exe = p.MainModule?.FileName; } catch { }
                    current = pending;
                    currentPid = pendingPid;
                    currentName = p.ProcessName;
                    pending = null;
                    Log.Info($"Jogo detectado: {current.Name} (pid {currentPid})");
                    GameStarted?.Invoke(current, MonitorOf(hwnd), exe);
                }
                return;
            }

            var map = new Dictionary<string, GameDef>(StringComparer.OrdinalIgnoreCase);
            foreach (var g in games())
                foreach (var name in g.Processes)
                    map.TryAdd(name, g);

            foreach (var p in Process.GetProcesses())
            {
                using (p)
                {
                    if (pending == null && map.TryGetValue(p.ProcessName, out var def))
                    {
                        pending = def;
                        pendingPid = p.Id;
                        pendingSince = DateTime.UtcNow;
                    }
                }
            }
        }
        catch (Exception ex) { Log.Error("Detector de jogos", ex); }
    }

    static bool IsAlive(int pid, string name)
    {
        try
        {
            using var p = Process.GetProcessById(pid);
            if (!string.Equals(p.ProcessName, name, StringComparison.OrdinalIgnoreCase)) return false;
            // Processos protegidos por anti-cheat podem negar o handle; existir na lista já basta.
            try { return !p.HasExited; } catch { return true; }
        }
        catch { return false; }
    }

    /// <summary>
    /// Converte a janela em índice de saída do ddagrab. O DXGI costuma listar o monitor principal primeiro;
    /// se não bater, o usuário pode fixar o índice em Configurações.
    /// </summary>
    static int MonitorOf(IntPtr hwnd)
    {
        if (hwnd == IntPtr.Zero) return 0;
        var screen = Screen.FromHandle(hwnd);
        if (screen.Primary) return 0;
        var others = Screen.AllScreens.Where(s => !s.Primary).ToList();
        int i = others.FindIndex(s => s.DeviceName == screen.DeviceName);
        return i < 0 ? 0 : i + 1;
    }

    public void Dispose() => timer.Dispose();
}
