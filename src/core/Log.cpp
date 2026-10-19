#include "Log.h"
#include "Paths.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>

#include <cstdarg>
#include <cstdio>
#include <mutex>

extern "C" {
#include <libavutil/log.h>
}

namespace aeonis::Log {

namespace {

std::mutex gate;

void write(const char *level, const QString &message)
{
    const QByteArray line = (QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
                             + QStringLiteral(" [%1] ").arg(QLatin1String(level)) + message + QStringLiteral("\r\n"))
                                .toUtf8();
    std::lock_guard lock(gate);
    QFile f(Paths::logFile());
    if (f.open(QIODevice::Append))
        f.write(line);
}

void qtHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    switch (type) {
    case QtDebugMsg:
        return; // qDebug só em depuração
    case QtInfoMsg: write("INFO", msg); break;
    case QtWarningMsg: write("WARN", msg); break;
    default: write("ERRO", msg); break;
    }
}

void avHandler(void *avcl, int level, const char *fmt, va_list vl)
{
    if (level > AV_LOG_WARNING)
        return;
    char line[1024];
    int prefix = 1;
    av_log_format_line(avcl, level, fmt, vl, line, sizeof(line), &prefix);
    QString text = QString::fromUtf8(line).trimmed();
    if (text.isEmpty())
        return;
    write(level <= AV_LOG_ERROR ? "ERRO" : "WARN", QStringLiteral("ffmpeg: ") + text);
}

} // namespace

void info(const QString &message) { write("INFO", message); }
void warn(const QString &message) { write("WARN", message); }
void error(const QString &message) { write("ERRO", message); }

void rotate()
{
    const QString path = Paths::logFile();
    QFileInfo fi(path);
    if (fi.exists() && fi.size() > 4'000'000) {
        QFile::remove(path + QStringLiteral(".old"));
        QFile::rename(path, path + QStringLiteral(".old"));
    }
}

void installHandlers()
{
    qInstallMessageHandler(qtHandler);
    av_log_set_level(AV_LOG_WARNING);
    av_log_set_callback(avHandler);
}

} // namespace aeonis::Log
