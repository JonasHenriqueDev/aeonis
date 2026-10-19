using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.Json;
using Aeonis.Core;
using Aeonis.Games;
using Microsoft.VisualBasic.FileIO;
using Microsoft.Web.WebView2.Core;
using Microsoft.Web.WebView2.WinForms;

namespace Aeonis.UI;

sealed record StatusDto(string State, string Game, string Encoder, string HotkeySave, string HotkeyRecord, string Version);

sealed record OptionDto(string Value, string Label);

sealed record FieldDto(string Key, string Category, string Label, string? Description, string Type, object? Value, List<OptionDto>? Options);

/// <summary>
/// Janela principal (biblioteca, player, configurações) feita em HTML dentro do WebView2.
/// Só existe enquanto está aberta: ao fechar, o WebView2 e seus processos são liberados,
/// então durante o jogo o custo é zero.
/// </summary>
sealed class MainWindow : Form
{
    static readonly JsonSerializerOptions Json = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };

    readonly WebView2 web = new() { Dock = DockStyle.Fill, DefaultBackgroundColor = Color.FromArgb(14, 15, 19) };
    readonly Func<Settings> settings;
    readonly Action<Settings> applySettings;
    readonly Func<StatusDto> status;
    CoreWebView2Environment? env;
    CancellationTokenSource? thumbCts;
    string pendingPage;
    string? mappedOutput;
    bool ready;

    public MainWindow(Func<Settings> settings, Action<Settings> applySettings, Func<StatusDto> status, string page)
    {
        this.settings = settings;
        this.applySettings = applySettings;
        this.status = status;
        pendingPage = page;

        Text = "Aeonis";
        Icon = AppIcon.Create(false);
        StartPosition = FormStartPosition.CenterScreen;
        Size = new Size(1380, 880);
        MinimumSize = new Size(980, 620);
        BackColor = Color.FromArgb(14, 15, 19);
        Controls.Add(web);

        HandleCreated += (_, _) => UseDarkTitleBar(Handle);
        Load += async (_, _) => await InitAsync();
        FormClosed += (_, _) =>
        {
            thumbCts?.Cancel();
            web.Dispose();
        };
    }

    async Task InitAsync()
    {
        try
        {
            env = await CoreWebView2Environment.CreateAsync(null, Path.Combine(Paths.AppData, "webview"));
            await web.EnsureCoreWebView2Async(env);
        }
        catch (WebView2RuntimeNotFoundException)
        {
            var r = MessageBox.Show(this,
                "A interface do Aeonis usa o Microsoft Edge WebView2, que não está instalado.\n\nAbrir a página de download?",
                "Aeonis", MessageBoxButtons.YesNo, MessageBoxIcon.Warning);
            if (r == DialogResult.Yes)
                Process.Start(new ProcessStartInfo("https://go.microsoft.com/fwlink/p/?LinkId=2124703") { UseShellExecute = true });
            Close();
            return;
        }

        var core = web.CoreWebView2;
        core.Settings.AreDefaultContextMenusEnabled = false;
        core.Settings.IsStatusBarEnabled = false;
        core.Settings.IsZoomControlEnabled = false;
        core.Settings.AreBrowserAcceleratorKeysEnabled = false;
        core.Settings.AreDevToolsEnabled = Debugger.IsAttached;
        core.SetVirtualHostNameToFolderMapping("thumbs.aeonis", Paths.Thumbs, CoreWebView2HostResourceAccessKind.Allow);
        core.SetVirtualHostNameToFolderMapping("icons.aeonis", Paths.Icons, CoreWebView2HostResourceAccessKind.Allow);
        MapMedia();
        core.AddWebResourceRequestedFilter("https://app.aeonis/*", CoreWebView2WebResourceContext.All);
        core.WebResourceRequested += ServeApp;
        core.WebMessageReceived += (_, e) => _ = OnMessageAsync(e.WebMessageAsJson);
        core.NewWindowRequested += (_, e) => e.Handled = true;
        core.Navigate("https://app.aeonis/index.html");
    }

    void MapMedia()
    {
        var dir = settings().OutputDir;
        if (string.Equals(dir, mappedOutput, StringComparison.OrdinalIgnoreCase)) return;
        Directory.CreateDirectory(dir);
        if (mappedOutput != null) web.CoreWebView2.ClearVirtualHostNameToFolderMapping("media.aeonis");
        web.CoreWebView2.SetVirtualHostNameToFolderMapping("media.aeonis", dir, CoreWebView2HostResourceAccessKind.Allow);
        mappedOutput = dir;
    }

    /// <summary>Serve index.html/app.css/app.js embutidos no próprio executável.</summary>
    void ServeApp(object? sender, CoreWebView2WebResourceRequestedEventArgs e)
    {
        var name = new Uri(e.Request.Uri).AbsolutePath.TrimStart('/');
        if (name.Length == 0) name = "index.html";
        var stream = typeof(MainWindow).Assembly.GetManifestResourceStream("web/" + name);
        if (stream == null)
        {
            e.Response = env!.CreateWebResourceResponse(null, 404, "Not Found", "");
            return;
        }
        var type = Path.GetExtension(name) switch
        {
            ".html" => "text/html; charset=utf-8",
            ".css" => "text/css; charset=utf-8",
            ".js" => "text/javascript; charset=utf-8",
            ".svg" => "image/svg+xml",
            _ => "application/octet-stream",
        };
        e.Response = env!.CreateWebResourceResponse(stream, 200, "OK", $"Content-Type: {type}\nCache-Control: no-store");
    }

    // ---------------- API usada pelo TrayApp ----------------

    public void ShowPage(string page)
    {
        pendingPage = page;
        if (ready) Post(new { type = "navigate", page });
        if (WindowState == FormWindowState.Minimized) WindowState = FormWindowState.Normal;
        Activate();
    }

    public void PushStatus()
    {
        if (ready) Post(new { type = "status", data = status() });
    }

    public void RefreshLibrary()
    {
        if (ready) _ = SendLibraryAsync();
    }

    // ---------------- Mensagens da página ----------------

    async Task OnMessageAsync(string json)
    {
        try
        {
            using var doc = JsonDocument.Parse(json);
            var r = doc.RootElement;
            switch (r.Get("cmd").Str())
            {
                case "ready":
                    ready = true;
                    SendSettings();
                    PushStatus();
                    Post(new { type = "navigate", page = pendingPage });
                    await SendLibraryAsync();
                    break;
                case "refresh":
                    await SendLibraryAsync();
                    break;
                case "reveal":
                    if (SafeVideo(r.Get("path").Str()) is { } rv) Process.Start("explorer.exe", $"/select,\"{rv}\"");
                    break;
                case "openExternal":
                    if (SafeVideo(r.Get("path").Str()) is { } op) Process.Start(new ProcessStartInfo(op) { UseShellExecute = true });
                    break;
                case "openFolder":
                    Directory.CreateDirectory(settings().OutputDir);
                    Process.Start("explorer.exe", $"\"{settings().OutputDir}\"");
                    break;
                case "delete":
                    Delete(r.Get("paths"));
                    await SendLibraryAsync();
                    break;
                case "trim":
                    await TrimAsync(r.Get("path").Str(), r.Get("start"), r.Get("end"));
                    break;
                case "saveSettings":
                    SaveSettings(r.Get("values"));
                    break;
                case "pick":
                    Pick(r.Get("key").Str(), r.Get("kind").Str());
                    break;
            }
        }
        catch (Exception ex)
        {
            Log.Error("Mensagem da interface", ex);
            Toast("Algo deu errado: " + ex.Message, "error");
        }
    }

    void Post(object message)
    {
        if (IsDisposed) return;
        if (InvokeRequired) { BeginInvoke(() => Post(message)); return; }
        web.CoreWebView2?.PostWebMessageAsJson(JsonSerializer.Serialize(message, Json));
    }

    void Toast(string text, string kind = "info") => Post(new { type = "toast", text, kind });

    async Task SendLibraryAsync()
    {
        MapMedia();
        var outputDir = settings().OutputDir;
        var lib = await Task.Run(() => LibraryModel.Build(outputDir));
        Post(new { type = "library", data = lib });

        thumbCts?.Cancel();
        var cts = thumbCts = new CancellationTokenSource();
        var missing = lib.Matches.SelectMany(m => m.Clips).Where(c => c.Thumb == null || c.Duration == null).ToList();
        _ = Task.Run(async () =>
        {
            foreach (var c in missing)
            {
                if (cts.IsCancellationRequested) return;
                var duration = await MediaCache.GenerateAsync(c.Path, c.Id);
                Post(new
                {
                    type = "thumb", id = c.Id,
                    url = MediaCache.HasThumb(c.Id) ? LibraryModel.ThumbUrl(c.Id) : null,
                    duration = c.Duration ?? duration,
                });
            }
        });
    }

    string? SafeVideo(string path)
    {
        if (string.IsNullOrEmpty(path)) return null;
        var full = Path.GetFullPath(path);
        var root = Path.GetFullPath(settings().OutputDir).TrimEnd('\\') + "\\";
        return full.StartsWith(root, StringComparison.OrdinalIgnoreCase)
               && full.EndsWith(".mp4", StringComparison.OrdinalIgnoreCase)
               && File.Exists(full) ? full : null;
    }

    void Delete(JsonElement? paths)
    {
        if (paths is not { ValueKind: JsonValueKind.Array } arr) return;
        int n = 0;
        foreach (var p in arr.EnumerateArray())
        {
            if (SafeVideo(p.GetString() ?? "") is not { } file) continue;
            try
            {
                FileSystem.DeleteFile(file, UIOption.OnlyErrorDialogs, RecycleOption.SendToRecycleBin);
                n++;
            }
            catch (Exception ex) { Log.Warn($"Excluir {file}: {ex.Message}"); }
        }
        Toast(n == 1 ? "Vídeo movido para a lixeira." : $"{n} vídeos movidos para a lixeira.");
    }

    async Task TrimAsync(string path, JsonElement? startEl, JsonElement? endEl)
    {
        if (SafeVideo(path) is not { } source) return;
        double start = startEl?.GetDouble() ?? 0, end = endEl?.GetDouble() ?? 0;
        if (end - start < 0.5) { Toast("O corte precisa ter pelo menos meio segundo.", "error"); return; }

        var dir = Path.GetDirectoryName(source)!;
        var name = Path.GetFileNameWithoutExtension(source);
        var output = Path.Combine(dir, name + "_corte.mp4");
        for (int i = 2; File.Exists(output); i++) output = Path.Combine(dir, $"{name}_corte{i}.mp4");
        string ss = start.ToString("0.###", CultureInfo.InvariantCulture), to = end.ToString("0.###", CultureInfo.InvariantCulture);

        var (code, log) = await Ffmpeg.RunAsync(
            $"-hide_banner -loglevel error -ss {ss} -to {to} -i {Ffmpeg.Q(source)} -map 0 -c copy -avoid_negative_ts make_zero -movflags +faststart -y {Ffmpeg.Q(output)}");
        if (code != 0 || !File.Exists(output))
        {
            Log.Error($"Falha ao cortar {source}: {log}");
            Post(new { type = "trimDone", ok = false });
            Toast("Falha ao cortar o vídeo (veja o log).", "error");
            return;
        }
        await Library.DerivedAsync(source, output, start);
        Post(new { type = "trimDone", ok = true, path = output });
        Toast("Corte salvo.");
        await SendLibraryAsync();
    }

    // ---------------- Configurações ----------------

    static readonly PropertyInfo[] SettingProps = typeof(Settings).GetProperties()
        .Where(p => p.CanWrite && p.GetCustomAttribute<CategoryAttribute>() != null).ToArray();

    void SendSettings()
    {
        var s = settings();
        var fields = SettingProps.Select(p =>
        {
            var editor = p.GetCustomAttribute<EditorAttribute>()?.EditorTypeName ?? "";
            var type = p.PropertyType == typeof(bool) ? "bool"
                : p.PropertyType.IsEnum ? "select"
                : p.PropertyType == typeof(int) ? "int"
                : p.PropertyType == typeof(double) ? "number"
                : editor.Contains("FolderNameEditor") ? "folder"
                : editor.Contains("FileNameEditor") ? "file"
                : editor.Contains("MultilineStringEditor") ? "multiline"
                : p.Name.StartsWith("Hotkey") ? "hotkey"
                : "text";
            var value = p.GetValue(s);
            List<OptionDto>? options = null;
            if (p.PropertyType.IsEnum)
            {
                options = Enum.GetNames(p.PropertyType).Select(n => new OptionDto(n,
                    p.PropertyType.GetField(n)!.GetCustomAttribute<DescriptionAttribute>()?.Description ?? n)).ToList();
                value = value?.ToString();
            }
            var category = p.GetCustomAttribute<CategoryAttribute>()!.Category;
            if (category.Length > 3 && char.IsDigit(category[0]) && category[1] == '.') category = category[3..];
            return new FieldDto(p.Name, category, p.GetCustomAttribute<DisplayNameAttribute>()?.DisplayName ?? p.Name,
                p.GetCustomAttribute<DescriptionAttribute>()?.Description, type, value, options);
        }).ToList();
        Post(new { type = "settings", fields });
    }

    void SaveSettings(JsonElement? values)
    {
        if (values is not { ValueKind: JsonValueKind.Object } obj) return;
        var updated = settings().Clone();
        foreach (var prop in obj.EnumerateObject())
        {
            var p = SettingProps.FirstOrDefault(x => x.Name == prop.Name);
            if (p == null) continue;
            var v = prop.Value;
            object? value = p.PropertyType switch
            {
                var t when t == typeof(bool) => v.ValueKind == JsonValueKind.True,
                var t when t == typeof(int) => (int)Math.Round(v.GetDouble()),
                var t when t == typeof(double) => v.GetDouble(),
                var t when t.IsEnum => Enum.Parse(t, v.GetString() ?? ""),
                _ => v.GetString() ?? "",
            };
            if (p.Name.StartsWith("Hotkey") && !HotkeyManager.TryParse((string)value!, out _, out _))
            {
                Toast($"Atalho inválido: {value}", "error");
                return;
            }
            p.SetValue(updated, value);
        }
        applySettings(updated.Normalize());
        SendSettings();
        Toast("Configurações salvas.");
        _ = SendLibraryAsync();
    }

    void Pick(string key, string kind)
    {
        string? value = null;
        if (kind == "folder")
        {
            using var dlg = new FolderBrowserDialog { UseDescriptionForTitle = true, Description = "Escolha a pasta" };
            if (dlg.ShowDialog(this) == DialogResult.OK) value = dlg.SelectedPath;
        }
        else
        {
            using var dlg = new OpenFileDialog { Filter = "ffmpeg.exe|ffmpeg.exe|Executáveis|*.exe" };
            if (dlg.ShowDialog(this) == DialogResult.OK) value = dlg.FileName;
        }
        if (value != null) Post(new { type = "picked", key, value });
    }

    // ---------------- Visual ----------------

    [DllImport("dwmapi.dll")]
    static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);

    static void UseDarkTitleBar(IntPtr hwnd)
    {
        int on = 1;
        if (DwmSetWindowAttribute(hwnd, 20, ref on, sizeof(int)) != 0) DwmSetWindowAttribute(hwnd, 19, ref on, sizeof(int));
    }
}
