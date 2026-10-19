using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using Aeonis.Core;
using Microsoft.VisualBasic.FileIO;

namespace Aeonis.UI;

/// <summary>Biblioteca de clipes: agrupados por jogo, miniaturas geradas sob demanda e cacheadas em disco.</summary>
sealed class LibraryForm : Form
{
    static readonly Size ThumbSize = new(192, 108);

    readonly Func<Settings> settings;
    readonly ListView list;
    readonly ImageList thumbs = new() { ImageSize = ThumbSize, ColorDepth = ColorDepth.Depth32Bit };
    readonly ToolStripLabel status = new();
    CancellationTokenSource? thumbCts;

    public LibraryForm(Func<Settings> settings)
    {
        this.settings = settings;
        Text = "Aeonis — Biblioteca";
        Icon = AppIcon.Create(false);
        StartPosition = FormStartPosition.CenterScreen;
        Size = new Size(1100, 700);

        list = new ListView
        {
            Dock = DockStyle.Fill,
            View = View.LargeIcon,
            LargeImageList = thumbs,
            MultiSelect = true,
            ShowGroups = true,
            ShowItemToolTips = true,
            BorderStyle = BorderStyle.None,
        };
        list.DoubleClick += (_, _) => Play();
        list.KeyDown += (_, e) =>
        {
            if (e.KeyCode == Keys.Delete) DeleteSelected();
            else if (e.KeyCode == Keys.Enter) Play();
            else if (e.KeyCode == Keys.F5) Reload();
        };

        var menu = new ContextMenuStrip();
        menu.Items.Add("Reproduzir", null, (_, _) => Play());
        menu.Items.Add("Mostrar na pasta", null, (_, _) => ShowInFolder());
        menu.Items.Add("Cortar…", null, (_, _) => Trim());
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("Excluir (lixeira)", null, (_, _) => DeleteSelected());
        list.ContextMenuStrip = menu;

        var bar = new ToolStrip { GripStyle = ToolStripGripStyle.Hidden, Padding = new Padding(6, 2, 6, 2) };
        bar.Items.Add(new ToolStripButton("Atualizar", null, (_, _) => Reload()));
        bar.Items.Add(new ToolStripButton("Abrir pasta", null, (_, _) => OpenFolder()));
        bar.Items.Add(new ToolStripSeparator());
        bar.Items.Add(status);

        Controls.Add(list);
        Controls.Add(bar);
        Load += (_, _) => Reload();
        FormClosed += (_, _) => { thumbCts?.Cancel(); thumbs.Dispose(); };
    }

    public void Reload()
    {
        thumbCts?.Cancel();
        thumbCts = new CancellationTokenSource();
        var token = thumbCts.Token;

        list.BeginUpdate();
        list.Items.Clear();
        list.Groups.Clear();
        foreach (Image img in thumbs.Images) img.Dispose();
        thumbs.Images.Clear();
        thumbs.Images.Add("_", Placeholder());

        var pending = new List<(ListViewItem Item, FileInfo File)>();
        var dir = settings().OutputDir;
        long total = 0;
        if (Directory.Exists(dir))
        {
            var files = new DirectoryInfo(dir).EnumerateFiles("*.mp4", System.IO.SearchOption.AllDirectories)
                .OrderByDescending(f => f.LastWriteTime).ToList();
            var groups = new Dictionary<string, ListViewGroup>();
            foreach (var f in files)
            {
                var gname = f.Directory?.Name ?? "";
                if (!groups.TryGetValue(gname, out var group))
                {
                    group = new ListViewGroup(gname, gname);
                    groups[gname] = group;
                    list.Groups.Add(group);
                }
                var item = new ListViewItem(Path.GetFileNameWithoutExtension(f.Name), "_", group)
                {
                    Tag = f.FullName,
                    ToolTipText = $"{f.LastWriteTime:dd/MM/yyyy HH:mm} — {f.Length / 1048576.0:0.0} MB",
                };
                list.Items.Add(item);
                pending.Add((item, f));
                total += f.Length;
            }
        }
        list.EndUpdate();
        status.Text = pending.Count == 0
            ? "Nenhum clipe ainda. Pressione " + settings().HotkeySaveClip + " durante um jogo."
            : $"{pending.Count} vídeos • {total / 1073741824.0:0.00} GB";
        _ = LoadThumbnailsAsync(pending, token);
    }

    async Task LoadThumbnailsAsync(List<(ListViewItem Item, FileInfo File)> items, CancellationToken token)
    {
        foreach (var (item, file) in items)
        {
            if (token.IsCancellationRequested || IsDisposed) return;
            try
            {
                var key = Convert.ToHexString(SHA1.HashData(Encoding.UTF8.GetBytes(
                    $"{file.FullName}|{file.Length}|{file.LastWriteTimeUtc.Ticks}")))[..20];
                var jpg = Path.Combine(Paths.Thumbs, key + ".jpg");
                if (!File.Exists(jpg)) await Ffmpeg.ThumbnailAsync(file.FullName, jpg);
                if (token.IsCancellationRequested || IsDisposed || !File.Exists(jpg)) continue;
                using var src = Image.FromStream(new MemoryStream(await File.ReadAllBytesAsync(jpg, token)));
                thumbs.Images.Add(key, Fit(src));
                item.ImageKey = key;
            }
            catch (OperationCanceledException) { return; }
            catch (Exception ex) { Log.Warn($"Miniatura de {file.Name}: {ex.Message}"); }
        }
    }

    static Bitmap Fit(Image src)
    {
        var bmp = new Bitmap(ThumbSize.Width, ThumbSize.Height);
        using var g = Graphics.FromImage(bmp);
        g.Clear(Color.Black);
        g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
        double scale = Math.Min(ThumbSize.Width / (double)src.Width, ThumbSize.Height / (double)src.Height);
        int w = (int)(src.Width * scale), h = (int)(src.Height * scale);
        g.DrawImage(src, (ThumbSize.Width - w) / 2, (ThumbSize.Height - h) / 2, w, h);
        return bmp;
    }

    static Bitmap Placeholder()
    {
        var bmp = new Bitmap(ThumbSize.Width, ThumbSize.Height);
        using var g = Graphics.FromImage(bmp);
        g.Clear(Color.FromArgb(30, 30, 36));
        using var b = new SolidBrush(Color.FromArgb(90, 90, 110));
        g.FillPolygon(b, new Point[] { new(84, 36), new(84, 72), new(114, 54) });
        return bmp;
    }

    IEnumerable<string> Selected() => list.SelectedItems.Cast<ListViewItem>().Select(i => (string)i.Tag!);

    void Play()
    {
        var f = Selected().FirstOrDefault();
        if (f != null) Process.Start(new ProcessStartInfo(f) { UseShellExecute = true });
    }

    void ShowInFolder()
    {
        var f = Selected().FirstOrDefault();
        if (f != null) Process.Start("explorer.exe", $"/select,\"{f}\"");
    }

    void OpenFolder()
    {
        var dir = settings().OutputDir;
        Directory.CreateDirectory(dir);
        Process.Start("explorer.exe", $"\"{dir}\"");
    }

    void Trim()
    {
        var f = Selected().FirstOrDefault();
        if (f == null) return;
        using var form = new TrimForm(f);
        if (form.ShowDialog(this) == DialogResult.OK) Reload();
    }

    void DeleteSelected()
    {
        var files = Selected().ToList();
        if (files.Count == 0) return;
        var msg = files.Count == 1 ? $"Mover \"{Path.GetFileName(files[0])}\" para a lixeira?" : $"Mover {files.Count} vídeos para a lixeira?";
        if (MessageBox.Show(this, msg, Text, MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return;
        foreach (var f in files)
        {
            try { FileSystem.DeleteFile(f, UIOption.OnlyErrorDialogs, RecycleOption.SendToRecycleBin); }
            catch (Exception ex) { Log.Warn($"Excluir {f}: {ex.Message}"); }
        }
        Reload();
    }
}
