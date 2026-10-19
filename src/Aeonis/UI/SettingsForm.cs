using Aeonis.Core;

namespace Aeonis.UI;

sealed class SettingsForm : Form
{
    public Settings Result { get; }

    public SettingsForm(Settings editable)
    {
        Result = editable;
        Text = "Aeonis — Configurações";
        Icon = AppIcon.Create(false);
        StartPosition = FormStartPosition.CenterScreen;
        Size = new Size(620, 720);
        MinimumSize = new Size(480, 480);

        var grid = new PropertyGrid
        {
            Dock = DockStyle.Fill,
            SelectedObject = editable,
            PropertySort = PropertySort.Categorized,
            ToolbarVisible = false,
            HelpVisible = true,
        };

        var ok = new Button { Text = "Salvar", DialogResult = DialogResult.OK, AutoSize = true };
        var cancel = new Button { Text = "Cancelar", DialogResult = DialogResult.Cancel, AutoSize = true };
        var note = new Label
        {
            Text = "Mudanças de captura (encoder, FPS, áudio) valem a partir da próxima sessão.",
            AutoSize = true,
            ForeColor = SystemColors.GrayText,
            Padding = new Padding(0, 8, 0, 0),
        };
        var bar = new FlowLayoutPanel
        {
            Dock = DockStyle.Bottom,
            FlowDirection = FlowDirection.RightToLeft,
            AutoSize = true,
            Padding = new Padding(8),
        };
        bar.Controls.AddRange([cancel, ok, note]);

        Controls.Add(grid);
        Controls.Add(bar);
        AcceptButton = ok;
        CancelButton = cancel;
    }
}
