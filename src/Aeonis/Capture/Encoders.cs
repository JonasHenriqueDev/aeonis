using Aeonis.Core;

namespace Aeonis.Capture;

/// <param name="FilterSuffix">
/// Filtros após o ddagrab. O ddagrab entrega frames D3D11 na GPU; NVENC e AMF consomem esses frames
/// diretamente (zero-copy). QSV precisa de hwmap e x264 precisa baixar o frame para a RAM.
/// </param>
sealed record VideoEncoder(EncoderChoice Choice, string Codec, string Label, string FilterSuffix, string ArgsTemplate)
{
    public bool IsHevc => Codec.StartsWith("hevc", StringComparison.Ordinal);
}

static class Encoders
{
    // {b} = bitrate em Mbps, {b2} = 2x bitrate (VBV), {g} = GOP em frames
    static readonly VideoEncoder[] All =
    [
        new(EncoderChoice.NvidiaH264, "h264_nvenc", "NVIDIA NVENC H.264", "",
            "-c:v h264_nvenc -preset p4 -tune hq -rc cbr -b:v {b}M -maxrate {b}M -bufsize {b2}M -g {g}"),
        new(EncoderChoice.NvidiaHevc, "hevc_nvenc", "NVIDIA NVENC HEVC", "",
            "-c:v hevc_nvenc -preset p4 -tune hq -rc cbr -b:v {b}M -maxrate {b}M -bufsize {b2}M -g {g}"),
        new(EncoderChoice.AmdH264, "h264_amf", "AMD AMF H.264", "",
            "-c:v h264_amf -usage transcoding -quality speed -rc cbr -b:v {b}M -maxrate {b}M -bufsize {b2}M -g {g}"),
        new(EncoderChoice.IntelH264, "h264_qsv", "Intel Quick Sync H.264", ",hwmap=derive_device=qsv,format=qsv",
            "-c:v h264_qsv -preset veryfast -b:v {b}M -maxrate {b}M -bufsize {b2}M -g {g}"),
        new(EncoderChoice.CpuX264, "libx264", "CPU x264 (pesado)", ",hwdownload,format=bgra",
            "-c:v libx264 -preset veryfast -b:v {b}M -maxrate {b}M -bufsize {b2}M -g {g} -pix_fmt yuv420p"),
    ];

    static VideoEncoder? autoDetected;

    public static async Task<VideoEncoder> ResolveAsync(EncoderChoice choice)
    {
        if (choice != EncoderChoice.Auto) return All.First(e => e.Choice == choice);
        if (autoDetected != null) return autoDetected;

        foreach (var e in All)
        {
            if (e.Choice is EncoderChoice.NvidiaHevc) continue;
            if (e.Choice is EncoderChoice.CpuX264) break;
            var (code, _) = await Ffmpeg.RunAsync(
                $"-hide_banner -loglevel error -f lavfi -i color=c=black:s=1280x720:r=30 -frames:v 5 -c:v {e.Codec} -f null -", 20_000)
                .ConfigureAwait(false);
            if (code == 0)
            {
                Log.Info($"Encoder detectado: {e.Label}");
                return autoDetected = e;
            }
        }
        Log.Warn("Nenhum encoder de hardware disponível; usando x264 na CPU.");
        return autoDetected = All[^1];
    }

    public static string Args(VideoEncoder e, int bitrateMbps, int gop) => e.ArgsTemplate
        .Replace("{b2}", (bitrateMbps * 2).ToString())
        .Replace("{b}", bitrateMbps.ToString())
        .Replace("{g}", gop.ToString());
}
