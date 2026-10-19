using System.Globalization;
using Aeonis.Core;

namespace Aeonis.UI;

/// <summary>Corte rápido sem re-encode (-c copy): instantâneo e sem perda de qualidade.</summary>
sealed class TrimForm : Form
{
    readonly string source;
    readonly NumericUpDown start = new() { DecimalPlaces = 1, Increment = 0.5m, Maximum = 100000, Width = 100 };
    readonly NumericUpDown end = new() { DecimalPlaces = 1, Increment = 0.5m, Maximum = 100000, Width = 100 };
    readonly Label info = new() { AutoSize = true, Text = "Lendo duração…" };
    readonly Button ok = new() { Text = "Cortar", AutoSize = true, Enabled = false };

    public string? Output { get; private set; }

    public TrimForm(string source)
    {
        this.source = source;
        Text = "Cortar clipe";
        Icon = AppIcon.Create(false);
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = MinimizeBox = false;
        StartPosition = FormStartPosition.CenterParent;
        AutoSize = true;
        AutoSizeMode = AutoSizeMode.GrowAndShrink;
        Padding = new Padding(12);

        var cancel = new Button { Text = "Cancelar", AutoSize = true, DialogResult = DialogResult.Cancel };
        var layout = new TableLayoutPanel { ColumnCount = 2, AutoSize = true, Dock = DockStyle.Fill };
        layout.Controls.Add(new Label { Text = Path.GetFileName(source), AutoSize = true, Font = new Font(Font, FontStyle.Bold) }, 0, 0);
        layout.SetColumnSpan(layout.GetControlFromPosition(0, 0)!, 2);
        layout.Controls.Add(info, 0, 1);
        layout.SetColumnSpan(info, 2);
        layout.Controls.Add(new Label { Text = "Início (s):", AutoSize = true, Anchor = AnchorStyles.Left }, 0, 2);
        layout.Controls.Add(start, 1, 2);
        layout.Controls.Add(new Label { Text = "Fim (s):", AutoSize = true, Anchor = AnchorStyles.Left }, 0, 3);
        layout.Controls.Add(end, 1, 3);
        var buttons = new FlowLayoutPanel { AutoSize = true, FlowDirection = FlowDirection.RightToLeft, Dock = DockStyle.Fill };
        buttons.Controls.AddRange([cancel, ok]);
        layout.Controls.Add(buttons, 0, 4);
        layout.SetColumnSpan(buttons, 2);
        Controls.Add(layout);
        CancelButton = cancel;

        Load += async (_, _) =>
        {
            var duration = await Ffmpeg.GetDurationAsync(source);
            if (IsDisposed) return;
            if (duration <= 0) { info.Text = "Não foi possível ler a duração do vídeo."; return; }
            var d = (decimal)Math.Round(duration, 1);
            start.Maximum = end.Maximum = d;
            end.Value = d;
            info.Text = $"Duração: {duration.ToString("0.0", CultureInfo.InvariantCulture)} s — o corte começa no keyframe mais próximo.";
            ok.Enabled = true;
        };
        ok.Click += async (_, _) => await TrimAsync();
    }

    async Task TrimAsync()
    {
        if (end.Value <= start.Value) { MessageBox.Show(this, "O fim precisa ser depois do início.", Text); return; }
        ok.Enabled = false;
        info.Text = "Cortando…";
        var dir = Path.GetDirectoryName(source)!;
        var name = Path.GetFileNameWithoutExtension(source);
        var output = Path.Combine(dir, name + "_corte.mp4");
        for (int i = 2; File.Exists(output); i++) output = Path.Combine(dir, $"{name}_corte{i}.mp4");
        string ss = start.Value.ToString(CultureInfo.InvariantCulture), to = end.Value.ToString(CultureInfo.InvariantCulture);
        var (code, log) = await Ffmpeg.RunAsync(
            $"-hide_banner -loglevel error -ss {ss} -to {to} -i {Ffmpeg.Q(source)} -map 0 -c copy -avoid_negative_ts make_zero -movflags +faststart -y {Ffmpeg.Q(output)}");
        if (code == 0 && File.Exists(output))
        {
            Output = output;
            DialogResult = DialogResult.OK;
            Close();
        }
        else
        {
            Log.Error($"Falha ao cortar {source}: {log}");
            info.Text = "Falha ao cortar (veja o log).";
            ok.Enabled = true;
        }
    }
}
