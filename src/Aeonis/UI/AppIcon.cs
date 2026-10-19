using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;

namespace Aeonis.UI;

/// <summary>Ícone desenhado em tempo de execução (sem arquivos de recurso). O ponto vermelho indica gravação longa.</summary>
static class AppIcon
{
    [DllImport("user32.dll")] static extern bool DestroyIcon(IntPtr handle);

    public static Icon Create(bool recording)
    {
        using var bmp = new Bitmap(32, 32);
        using (var g = Graphics.FromImage(bmp))
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;
            g.Clear(Color.Transparent);
            using var brush = new LinearGradientBrush(new Rectangle(0, 0, 32, 32),
                Color.FromArgb(124, 58, 237), Color.FromArgb(14, 165, 233), 45f);
            g.FillEllipse(brush, 1, 1, 30, 30);
            using var font = new Font("Segoe UI", 18, FontStyle.Bold, GraphicsUnit.Pixel);
            using var fmt = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center };
            g.DrawString("A", font, Brushes.White, new RectangleF(0, 1, 32, 32), fmt);
            if (recording)
            {
                g.FillEllipse(Brushes.White, 19, 19, 13, 13);
                g.FillEllipse(Brushes.Red, 21, 21, 9, 9);
            }
        }
        IntPtr h = bmp.GetHicon();
        try { return (Icon)Icon.FromHandle(h).Clone(); }
        finally { DestroyIcon(h); }
    }
}
