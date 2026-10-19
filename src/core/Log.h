#pragma once
// Log em %LOCALAPPDATA%\Aeonis\aeonis.log, no mesmo formato da versão C#. Thread-safe.
// Também recebe as mensagens do Qt (qWarning etc.) e do FFmpeg.

#include <QString>

namespace aeonis::Log {

void info(const QString &message);
void warn(const QString &message);
void error(const QString &message);
/// Renomeia o log para .old quando passa de 4 MB.
void rotate();
/// Redireciona qDebug/qWarning e o av_log do FFmpeg para o arquivo.
void installHandlers();

} // namespace aeonis::Log
