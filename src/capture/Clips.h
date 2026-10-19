#pragma once
// Nomes dos arquivos de vídeo, vídeos "salvos" (★, protegidos) e o limite de armazenamento.

#include <QString>
#include <QStringList>

namespace aeonis::Clips {

QString sanitize(const QString &s);
/// <pasta>\<Jogo>\<Jogo>_aaaa-MM-dd_HH-mm-ss_<rótulo>.mp4 (sem sobrescrever).
QString newPath(const QString &outputDir, const QString &game, const QString &label);

/// Vídeos salvos nunca são apagados pelo limite de armazenamento (lista em saved.json).
bool isSaved(const QString &path);
void setSaved(const QStringList &paths, bool value);

/// Apaga os vídeos não salvos mais antigos até eles ocuparem no máximo `maxGb`.
/// Nunca apaga o mais recente. Devolve quantos foram removidos.
int enforceQuota(const QString &outputDir, double maxGb);
/// Tamanho dos vídeos não salvos (o que conta para o limite).
qint64 unsavedBytes(const QString &outputDir);

} // namespace aeonis::Clips
