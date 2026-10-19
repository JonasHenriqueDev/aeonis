using System.Runtime.InteropServices;

namespace Aeonis.UI;

/// <summary>Atalhos globais via RegisterHotKey (funcionam com o jogo em foco, sem hook de teclado).</summary>
sealed class HotkeyManager : NativeWindow, IDisposable
{
    [DllImport("user32.dll", SetLastError = true)] static extern bool RegisterHotKey(IntPtr hWnd, int id, uint modifiers, uint vk);
    [DllImport("user32.dll")] static extern bool UnregisterHotKey(IntPtr hWnd, int id);

    const int WM_HOTKEY = 0x0312;
    const uint MOD_ALT = 1, MOD_CONTROL = 2, MOD_SHIFT = 4, MOD_WIN = 8, MOD_NOREPEAT = 0x4000;

    readonly Dictionary<int, Action> actions = [];
    int nextId = 1;

    public HotkeyManager() => CreateHandle(new CreateParams());

    public bool Register(string combo, Action action, out string? error)
    {
        if (!TryParse(combo, out var mods, out var vk))
        {
            error = $"Atalho inválido: \"{combo}\"";
            return false;
        }
        int id = nextId++;
        if (!RegisterHotKey(Handle, id, mods | MOD_NOREPEAT, vk))
        {
            error = $"O atalho {combo} já está em uso por outro programa.";
            return false;
        }
        actions[id] = action;
        error = null;
        return true;
    }

    public void UnregisterAll()
    {
        foreach (var id in actions.Keys) UnregisterHotKey(Handle, id);
        actions.Clear();
    }

    protected override void WndProc(ref Message m)
    {
        if (m.Msg == WM_HOTKEY && actions.TryGetValue((int)m.WParam, out var action)) action();
        base.WndProc(ref m);
    }

    public static bool TryParse(string combo, out uint mods, out uint vk)
    {
        mods = 0;
        vk = 0;
        foreach (var part in (combo ?? "").Split('+', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            switch (part.ToLowerInvariant())
            {
                case "ctrl" or "control": mods |= MOD_CONTROL; break;
                case "alt": mods |= MOD_ALT; break;
                case "shift": mods |= MOD_SHIFT; break;
                case "win": mods |= MOD_WIN; break;
                default:
                    var key = part.Length == 1 && char.IsDigit(part[0]) ? "D" + part : part;
                    if (!Enum.TryParse<Keys>(key, true, out var k)) return false;
                    vk = (uint)k;
                    break;
            }
        }
        return vk != 0;
    }

    public void Dispose()
    {
        UnregisterAll();
        DestroyHandle();
    }
}
