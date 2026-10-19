#include "CaptureEngine.h"
#include "Clips.h"
#include "DesktopDuplication.h"
#include "Encoders.h"
#include "SegmentWriter.h"
#include "audio/AudioCapture.h"
#include "audio/AudioMixer.h"
#include "core/Log.h"
#include "core/Media.h"
#include "core/Paths.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

#include <algorithm>
#include <cmath>

#include <avrt.h>
#include <timeapi.h>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace aeonis {

namespace {

const QString PinFile = QStringLiteral("recording.json");

qint64 qpcNow()
{
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

qint64 qpcFrequency()
{
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    return f.QuadPart;
}

int keepSecondsFor(const Settings &s)
{
    return std::max(s.replaySeconds, s.clipPreSeconds + s.clipPostSeconds + CaptureEngine::MaxExtendSeconds) + 10;
}

/// Mesmo formato do PinInfo da versão C# (Game, Start, OutputDir): recupera buffers das duas versões.
void writePin(const QString &dir, const QString &game, int start, const QString &outputDir)
{
    QJsonObject o;
    o.insert(QStringLiteral("Game"), game);
    o.insert(QStringLiteral("Start"), start);
    o.insert(QStringLiteral("OutputDir"), outputDir);
    QSaveFile f(dir + QLatin1Char('/') + PinFile);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

} // namespace

CaptureEngine::CaptureEngine(QObject *parent) : QObject(parent) {}

CaptureEngine::~CaptureEngine() { stopForShutdown(); }

std::vector<CaptureEngine::Segment> CaptureEngine::listSegments(const QString &dir)
{
    std::vector<Segment> result;
    QDirIterator it(dir, {QStringLiteral("seg_*.ts")}, QDir::Files);
    while (it.hasNext()) {
        const QFileInfo fi = it.nextFileInfo();
        bool ok = false;
        const int idx = fi.completeBaseName().mid(4).toInt(&ok);
        if (ok)
            result.push_back({idx, QDir::toNativeSeparators(fi.absoluteFilePath())});
    }
    std::sort(result.begin(), result.end(), [](const Segment &a, const Segment &b) { return a.index < b.index; });
    return result;
}

ClipResult CaptureEngine::concat(const std::vector<Segment> &segments, const QString &output)
{
    QStringList files;
    for (const Segment &s : segments)
        files << s.path;
    const QElapsedTimer timer = [] { QElapsedTimer t; t.start(); return t; }();
    const Media::RemuxResult r = Media::concat(files, output);
    if (!r.ok) {
        Log::error(QStringLiteral("Falha ao gerar %1: %2").arg(output, r.error));
        return {};
    }
    Log::info(QStringLiteral("Salvo: %1 (%2 segmentos, %3 s, clip saved in %4 ms)")
                  .arg(output).arg(segments.size()).arg(r.duration, 0, 'f', 1).arg(timer.elapsed()));
    return {output, r.duration};
}

bool CaptureEngine::start(const QString &game, HMONITOR monitor, const Settings &settings, QString *error)
{
    std::lock_guard lock(m_mutex);
    if (m_running)
        return true;
    m_settings = settings;
    m_monitor = monitor;
    m_dir = QDir::toNativeSeparators(Paths::buffer() + QLatin1Char('/')
                                     + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")));
    QDir().mkpath(m_dir);

    m_dup = std::make_unique<DesktopDuplication>();
    if (!m_dup->open(monitor, error)) {
        m_dup.reset();
        QDir(m_dir).removeRecursively();
        return false;
    }
    const QSize size = m_dup->size();
    m_encoder = VideoEncoder::open(settings.encoder, m_dup->device(), m_dup->adapterLuid(), size.width(), size.height(),
                                   settings.fps, settings.bitrateMbps, error);
    if (!m_encoder) {
        m_dup.reset();
        QDir(m_dir).removeRecursively();
        return false;
    }
    m_encoderLabel = m_encoder->info().label;

    std::vector<AudioCapture *> sources;
    m_audio.clear();
    if (settings.systemAudio)
        m_audio.push_back(std::make_unique<AudioCapture>(AudioCapture::Kind::Loopback, QString()));
    if (settings.microphone)
        m_audio.push_back(std::make_unique<AudioCapture>(AudioCapture::Kind::Microphone, settings.micDevice));
    for (auto it = m_audio.begin(); it != m_audio.end();) {
        QString err;
        if ((*it)->start(&err)) {
            sources.push_back(it->get());
            ++it;
        } else {
            Log::error(QStringLiteral("Áudio indisponível: %1").arg(err));
            it = m_audio.erase(it);
        }
    }
    m_mixer.reset();
    if (!sources.empty()) {
        m_mixer = std::make_unique<AudioMixer>();
        QString err;
        if (!m_mixer->open(&err)) {
            Log::error(err);
            m_mixer.reset();
        }
    }

    m_writer = std::make_unique<SegmentWriter>();
    if (!m_writer->open(m_dir, 0, m_encoder->context(), m_mixer ? m_mixer->context() : nullptr, error)) {
        for (auto &a : m_audio) a->stop();
        m_audio.clear();
        m_mixer.reset();
        m_encoder.reset();
        m_dup.reset();
        m_writer.reset();
        QDir(m_dir).removeRecursively();
        return false;
    }

    m_game = game;
    m_sessionId = QUuid::createUuid().toString(QUuid::Id128).left(12);
    m_pinStart.reset();
    m_recordingSince = {};
    m_clockStart = qpcNow();
    if (m_mixer) {
        SegmentWriter *writer = m_writer.get();
        m_mixer->start(sources, m_clockStart, [writer](AVPacket *pkt) { writer->writeAudio(pkt); });
    }
    m_videoRunning = true;
    m_videoThread = std::thread(&CaptureEngine::videoLoop, this);
    {
        std::lock_guard j(m_janitorMutex);
        m_janitorStop = false;
    }
    m_janitorThread = std::thread(&CaptureEngine::janitorLoop, this);
    m_running = true;
    Log::info(QStringLiteral("Captura iniciada: %1 (%2x%3, %4, %5 fps, %6 Mbps, áudio: %7)")
                  .arg(game).arg(size.width()).arg(size.height()).arg(m_encoderLabel).arg(settings.fps)
                  .arg(settings.bitrateMbps).arg(sources.size()));
    return true;
}

void CaptureEngine::videoLoop()
{
    timeBeginPeriod(1);
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Capture", &taskIndex);
    if (!mmcss)
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);

    const qint64 freq = qpcFrequency();
    const int fps = m_settings.fps;
    const bool drawMouse = m_settings.drawMouse;
    const int64_t keyInterval = int64_t(fps) * SegmentSeconds;
    int64_t lastPts = -1;
    int64_t nextKey = 0;
    int failures = 0;
    int restarts = 0;
    SegmentWriter *writer = m_writer.get();
    const VideoEncoder::Output out = [writer](AVPacket *pkt) { writer->writeVideo(pkt); };
    QString lastError;

    while (m_videoRunning) {
        // Espera o instante do próximo quadro, absorvendo os quadros novos da tela no caminho:
        // o encoder sempre recebe a imagem mais recente, num ritmo constante de `fps`.
        const qint64 due = m_clockStart + (lastPts + 1) * freq / fps;
        for (;;) {
            const qint64 remainingMs = (due - qpcNow()) * 1000 / freq;
            if (remainingMs <= 0 || !m_videoRunning)
                break;
            const auto r = m_dup->acquire(int(std::min<qint64>(remainingMs, 50)));
            if (r == DesktopDuplication::Result::Lost)
                Sleep(DWORD(std::min<qint64>(remainingMs, 50)));
        }
        if (!m_videoRunning)
            break;
        const int64_t pts = std::max(lastPts + 1, (qpcNow() - m_clockStart) * fps / freq);
        lastPts = pts;
        const bool key = pts >= nextKey;
        if (key)
            nextKey = (pts / keyInterval + 1) * keyInterval;
        if (m_encoder->encode(m_dup->compose(drawMouse), pts, key, out)) {
            failures = 0;
            continue;
        }
        if (++failures < 3)
            continue;
        // O encoder parou (driver, GPU reiniciada): reabre até 3 vezes, como a versão C# fazia com o ffmpeg.
        failures = 0;
        if (restarts++ < 3 && reopenEncoder(&lastError)) {
            writer = m_writer.get();
            nextKey = 0;
            continue;
        }
        m_videoRunning = false;
        const QString message = QStringLiteral("A captura parou: %1").arg(lastError.isEmpty() ? QStringLiteral("o encoder de vídeo falhou") : lastError);
        Log::error(message);
        QMetaObject::invokeMethod(this, [this, message] { emit failed(message); }, Qt::QueuedConnection);
        break;
    }
    if (m_encoder)
        m_encoder->flush(out);
    if (mmcss)
        AvRevertMmThreadCharacteristics(mmcss);
    timeEndPeriod(1);
}

bool CaptureEngine::reopenEncoder(QString *error)
{
    // Chamado na thread de vídeo. O áudio continua escrevendo no gravador (protegido por mutex).
    const int next = m_writer->currentIndex() + 1;
    m_writer->close();
    m_encoder.reset();
    const QSize size = m_dup->size();
    m_encoder = VideoEncoder::open(m_settings.encoder, m_dup->device(), m_dup->adapterLuid(), size.width(),
                                   size.height(), m_settings.fps, m_settings.bitrateMbps, error);
    if (!m_encoder)
        return false;
    if (!m_writer->open(m_dir, next, m_encoder->context(), m_mixer ? m_mixer->context() : nullptr, error))
        return false;
    Log::warn(QStringLiteral("Encoder reaberto (%1).").arg(m_encoder->info().label));
    return true;
}

void CaptureEngine::janitorLoop()
{
    std::unique_lock lock(m_janitorMutex);
    while (!m_janitorStop) {
        m_janitorCv.wait_for(lock, std::chrono::seconds(2));
        if (m_janitorStop)
            break;
        if (m_busy.load() > 0)
            continue;
        QString dir;
        int keepFrom = 0;
        {
            std::lock_guard state(m_mutex);
            dir = m_dir;
            const auto segments = listSegments(dir);
            if (segments.empty())
                continue;
            keepFrom = segments.back().index - keepSecondsFor(m_settings) / SegmentSeconds;
            if (m_pinStart)
                keepFrom = std::min(keepFrom, *m_pinStart);
            for (const Segment &seg : segments) {
                if (seg.index >= keepFrom)
                    break;
                QFile::remove(seg.path);
            }
        }
    }
}

void CaptureEngine::stopThreads()
{
    m_videoRunning = false;
    if (m_videoThread.joinable())
        m_videoThread.join();
    if (m_mixer)
        m_mixer->stop(); // esvazia o AAC no gravador
    for (auto &a : m_audio)
        a->stop();
    if (m_writer)
        m_writer->close();
    {
        std::lock_guard j(m_janitorMutex);
        m_janitorStop = true;
    }
    m_janitorCv.notify_all();
    if (m_janitorThread.joinable())
        m_janitorThread.join();
    m_audio.clear();
    m_mixer.reset();
    m_encoder.reset();
    m_writer.reset();
    m_dup.reset();
}

ClipResult CaptureEngine::stop()
{
    QString dir;
    {
        std::lock_guard lock(m_mutex);
        if (!m_running)
            return {};
        dir = m_dir;
    }
    stopThreads();
    ClipResult recording;
    if (this->recording())
        recording = finishRecording(dir);
    {
        std::lock_guard lock(m_mutex);
        m_running = false;
        m_dir.clear();
    }
    if (!QDir(dir).removeRecursively())
        Log::warn(QStringLiteral("Não foi possível limpar %1").arg(dir));
    Log::info(QStringLiteral("Captura encerrada."));
    return recording;
}

void CaptureEngine::stopForShutdown()
{
    {
        std::lock_guard lock(m_mutex);
        if (!m_running)
            return;
    }
    stopThreads();
    std::lock_guard lock(m_mutex);
    const QString dir = m_dir;
    m_running = false;
    m_dir.clear();
    if (!m_pinStart)
        QDir(dir).removeRecursively();
    m_pinStart.reset();
    Log::info(QStringLiteral("Captura encerrada (sistema desligando)."));
}

ClipResult CaptureEngine::saveClip(int seconds, const QString &label)
{
    std::vector<Segment> pick;
    QString game, outputDir;
    {
        std::lock_guard lock(m_mutex);
        if (!m_running)
            return {};
        ++m_busy;
        const auto segments = listSegments(m_dir);
        const size_t need = size_t(std::ceil(seconds / double(SegmentSeconds))) + 1;
        const size_t from = segments.size() > need ? segments.size() - need : 0;
        pick.assign(segments.begin() + from, segments.end());
        game = m_game;
        outputDir = m_settings.outputDir;
    }
    ClipResult r;
    if (!pick.empty())
        r = concat(pick, Clips::newPath(outputDir, game, label));
    --m_busy;
    return r;
}

void CaptureEngine::beginRecording()
{
    std::lock_guard lock(m_mutex);
    if (!m_running || m_pinStart)
        return;
    const auto segments = listSegments(m_dir);
    m_pinStart = segments.empty() ? 0 : segments.back().index;
    m_pinOutputDir = m_settings.outputDir;
    m_recordingSince = QDateTime::currentDateTime();
    writePin(m_dir, m_game, *m_pinStart, m_pinOutputDir);
    Log::info(QStringLiteral("Gravação longa iniciada a partir do segmento %1.").arg(*m_pinStart));
}

ClipResult CaptureEngine::endRecording()
{
    QString dir;
    {
        std::lock_guard lock(m_mutex);
        if (!m_running || !m_pinStart)
            return {};
        dir = m_dir;
    }
    return finishRecording(dir);
}

ClipResult CaptureEngine::finishRecording(const QString &dir)
{
    std::vector<Segment> pick;
    QString game, outputDir;
    {
        std::lock_guard lock(m_mutex);
        if (!m_pinStart)
            return {};
        ++m_busy;
        for (const Segment &s : listSegments(dir))
            if (s.index >= *m_pinStart)
                pick.push_back(s);
        game = m_game;
        outputDir = m_pinOutputDir.isEmpty() ? m_settings.outputDir : m_pinOutputDir;
    }
    const QString label = game == QLatin1String("Desktop") ? QStringLiteral("Gravação") : QStringLiteral("Partida");
    ClipResult r;
    if (!pick.empty())
        r = concat(pick, Clips::newPath(outputDir, game, label));
    {
        std::lock_guard lock(m_mutex);
        m_pinStart.reset();
        m_recordingSince = {};
        QFile::remove(dir + QLatin1Char('/') + PinFile);
    }
    --m_busy;
    return r;
}

void CaptureEngine::updateSettings(const Settings &settings)
{
    std::lock_guard lock(m_mutex);
    // Encoder, FPS, monitor e áudio só mudam na próxima sessão; o resto vale já.
    const Settings old = m_settings;
    m_settings = settings;
    m_settings.encoder = old.encoder;
    m_settings.fps = old.fps;
    m_settings.bitrateMbps = old.bitrateMbps;
    m_settings.drawMouse = old.drawMouse;
}

bool CaptureEngine::recording() const
{
    std::lock_guard lock(m_mutex);
    return m_pinStart.has_value();
}

QString CaptureEngine::game() const
{
    std::lock_guard lock(m_mutex);
    return m_game;
}

QString CaptureEngine::sessionId() const
{
    std::lock_guard lock(m_mutex);
    return m_sessionId;
}

QString CaptureEngine::encoderLabel() const
{
    std::lock_guard lock(m_mutex);
    return m_encoderLabel;
}

QDateTime CaptureEngine::recordingSince() const
{
    std::lock_guard lock(m_mutex);
    return m_recordingSince;
}

void CaptureEngine::recoverLeftovers(const std::function<void(const ClipResult &, const QString &)> &recovered)
{
    const QFileInfoList dirs = QDir(Paths::buffer()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &d : dirs) {
        const QString dir = d.absoluteFilePath();
        QFile pinFile(dir + QLatin1Char('/') + PinFile);
        if (pinFile.open(QIODevice::ReadOnly)) {
            QByteArray data = pinFile.readAll();
            pinFile.close();
            if (data.startsWith("\xEF\xBB\xBF"))
                data.remove(0, 3);
            const QJsonObject pin = QJsonDocument::fromJson(data).object();
            const QString game = pin.value(QLatin1String("Game")).toString(QStringLiteral("Desktop"));
            const int start = pin.value(QLatin1String("Start")).toInt();
            QString outputDir = pin.value(QLatin1String("OutputDir")).toString();
            if (outputDir.isEmpty())
                outputDir = Settings::load().outputDir;
            std::vector<Segment> segs;
            for (const Segment &s : listSegments(dir))
                if (s.index >= start)
                    segs.push_back(s);
            if (!segs.empty()) {
                const ClipResult r = concat(segs, Clips::newPath(outputDir, game, QStringLiteral("Recuperado")));
                if (!r)
                    continue; // mantém os segmentos para nova tentativa
                if (recovered)
                    recovered(r, game);
            }
        }
        if (!QDir(dir).removeRecursively())
            Log::warn(QStringLiteral("Limpeza de %1 incompleta").arg(dir));
    }
}

} // namespace aeonis
