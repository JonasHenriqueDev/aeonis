using System.Diagnostics;
using System.IO.Pipes;
using Aeonis.Core;
using NAudio.CoreAudioApi;
using NAudio.Wave;

namespace Aeonis.Capture;

/// <summary>
/// Captura áudio via WASAPI (loopback do sistema ou microfone) e entrega PCM cru ao FFmpeg por um named pipe.
/// O WASAPI loopback não entrega nada quando não há som tocando, então o escritor completa com silêncio
/// para que o FFmpeg receba um fluxo contínuo em tempo real e o áudio não dessincronize do vídeo.
/// </summary>
sealed class AudioPipe : IDisposable
{
    static readonly Guid FloatSubFormat = new("00000003-0000-0010-8000-00aa00389b71");

    readonly MMDeviceEnumerator enumerator = new();
    readonly IWaveIn capture;
    readonly NamedPipeServerStream pipe;
    readonly ByteRing ring = new(1 << 20);
    readonly Thread writer;
    readonly CancellationTokenSource cts = new();
    long lastDataTimestamp;

    public WaveFormat Format { get; }
    public string PipePath { get; }
    public string DeviceName { get; }

    public static AudioPipe? TryCreate(bool loopback, string? deviceHint, string tag)
    {
        try { return new AudioPipe(loopback, deviceHint, tag); }
        catch (Exception ex)
        {
            Log.Error($"Áudio '{tag}' indisponível", ex);
            return null;
        }
    }

    AudioPipe(bool loopback, string? deviceHint, string tag)
    {
        var device = loopback
            ? enumerator.GetDefaultAudioEndpoint(DataFlow.Render, Role.Multimedia)
            : FindMicrophone(deviceHint);
        DeviceName = device.FriendlyName;
        capture = loopback ? new WasapiLoopbackCapture(device) : new WasapiCapture(device, true, 20);
        Format = capture.WaveFormat;

        var name = $"aeonis_{tag}_{Environment.ProcessId}_{Environment.TickCount64}";
        PipePath = @"\\.\pipe\" + name;
        pipe = new NamedPipeServerStream(name, PipeDirection.Out, 1, PipeTransmissionMode.Byte,
            PipeOptions.Asynchronous, 0, 1 << 20);

        capture.DataAvailable += (_, e) =>
        {
            if (e.BytesRecorded <= 0) return;
            ring.Write(e.Buffer, 0, e.BytesRecorded);
            Interlocked.Exchange(ref lastDataTimestamp, Stopwatch.GetTimestamp());
        };
        writer = new Thread(WriterLoop) { IsBackground = true, Name = "aeonis-audio-" + tag, Priority = ThreadPriority.AboveNormal };
        Log.Info($"Áudio '{tag}': {DeviceName} [{Format}]");
    }

    MMDevice FindMicrophone(string? hint)
    {
        if (!string.IsNullOrWhiteSpace(hint))
        {
            foreach (var d in enumerator.EnumerateAudioEndPoints(DataFlow.Capture, DeviceState.Active))
                if (d.FriendlyName.Contains(hint, StringComparison.OrdinalIgnoreCase)) return d;
            Log.Warn($"Microfone '{hint}' não encontrado; usando o padrão.");
        }
        return enumerator.GetDefaultAudioEndpoint(DataFlow.Capture, Role.Console);
    }

    /// <summary>Argumentos de entrada do FFmpeg que descrevem exatamente o PCM que este pipe entrega.</summary>
    public string InputArgs()
    {
        bool isFloat = Format.Encoding == WaveFormatEncoding.IeeeFloat
                       || (Format is WaveFormatExtensible ext && ext.SubFormat == FloatSubFormat);
        string sampleFormat = isFloat
            ? (Format.BitsPerSample == 64 ? "f64le" : "f32le")
            : Format.BitsPerSample switch { 24 => "s24le", 32 => "s32le", _ => "s16le" };
        string layout = Format.Channels switch
        {
            1 => "mono", 2 => "stereo", 3 => "2.1", 4 => "quad", 6 => "5.1", 8 => "7.1",
            _ => $"{Format.Channels}c",
        };
        // Sem sondagem: o formato já é conhecido, e sondar faria o FFmpeg engolir segundos de áudio antes do
        // primeiro frame de vídeo (atrasando a captura e dessincronizando o áudio).
        return $"-thread_queue_size 4096 -probesize 32 -analyzeduration 0 -f {sampleFormat} -sample_rate {Format.SampleRate} -ch_layout {layout} -i {PipePath}";
    }

    public void Start()
    {
        capture.StartRecording();
        writer.Start();
    }

    void WriterLoop()
    {
        try { pipe.WaitForConnectionAsync(cts.Token).GetAwaiter().GetResult(); }
        catch { return; }

        int align = Format.BlockAlign;
        double bytesPerSecond = Format.AverageBytesPerSecond;
        int Align(double n) => (int)(n / align) * align;

        var buffer = new byte[Align(bytesPerSecond * 0.25)];
        var zeros = new byte[buffer.Length];
        int maxBacklog = Align(bytesPerSecond * 0.30);
        int keepBacklog = Align(bytesPerSecond * 0.06);
        int farBehind = Align(bytesPerSecond * 0.12);
        long silenceTicks = Stopwatch.Frequency * 80 / 1000;

        long start = Stopwatch.GetTimestamp();
        long written = 0;
        try
        {
            while (!cts.IsCancellationRequested)
            {
                Thread.Sleep(10);
                long now = Stopwatch.GetTimestamp();
                long target = (long)((now - start) * bytesPerSecond / Stopwatch.Frequency) / align * align;
                long due = target - written;
                if (due <= 0) continue;

                // Se a captura acumulou atraso (ex.: FFmpeg demorou a conectar), descarta o excesso.
                int backlog = ring.Count;
                if (backlog > maxBacklog) ring.Discard(backlog - keepBacklog);

                while (due > 0)
                {
                    int n = ring.Read(buffer, 0, (int)Math.Min(due, buffer.Length));
                    if (n <= 0) break;
                    pipe.Write(buffer, 0, n);
                    written += n;
                    due -= n;
                }

                bool silent = now - Interlocked.Read(ref lastDataTimestamp) > silenceTicks;
                if (due > 0 && (silent || due > farBehind))
                {
                    while (due > 0)
                    {
                        int n = (int)Math.Min(due, zeros.Length);
                        pipe.Write(zeros, 0, n);
                        written += n;
                        due -= n;
                    }
                }
            }
        }
        catch (IOException) { }
        catch (ObjectDisposedException) { }
        catch (Exception ex) { Log.Error("Escritor de áudio", ex); }
    }

    public void Dispose()
    {
        cts.Cancel();
        try { capture.StopRecording(); } catch { }
        try { capture.Dispose(); } catch { }
        try { pipe.Dispose(); } catch { }
        writer.Join(1000);
        enumerator.Dispose();
        cts.Dispose();
    }
}

/// <summary>Fila circular de bytes thread-safe, cresce sob demanda.</summary>
sealed class ByteRing(int capacity)
{
    byte[] buf = new byte[capacity];
    int head, count;
    readonly object gate = new();

    public int Count { get { lock (gate) return count; } }

    public void Write(byte[] src, int offset, int length)
    {
        lock (gate)
        {
            if (count + length > 8 << 20) { head = 0; count = 0; }
            if (count + length > buf.Length) Grow(count + length);
            int tail = (head + count) % buf.Length;
            int first = Math.Min(length, buf.Length - tail);
            Buffer.BlockCopy(src, offset, buf, tail, first);
            Buffer.BlockCopy(src, offset + first, buf, 0, length - first);
            count += length;
        }
    }

    public int Read(byte[] dst, int offset, int max)
    {
        lock (gate)
        {
            int n = Math.Min(max, count);
            int first = Math.Min(n, buf.Length - head);
            Buffer.BlockCopy(buf, head, dst, offset, first);
            Buffer.BlockCopy(buf, 0, dst, offset + first, n - first);
            head = (head + n) % buf.Length;
            count -= n;
            return n;
        }
    }

    public void Discard(int n)
    {
        lock (gate)
        {
            n = Math.Min(n, count);
            head = (head + n) % buf.Length;
            count -= n;
        }
    }

    void Grow(int needed)
    {
        int cap = buf.Length;
        while (cap < needed) cap *= 2;
        var nb = new byte[cap];
        int first = Math.Min(count, buf.Length - head);
        Buffer.BlockCopy(buf, head, nb, 0, first);
        Buffer.BlockCopy(buf, 0, nb, first, count - first);
        buf = nb;
        head = 0;
    }
}
