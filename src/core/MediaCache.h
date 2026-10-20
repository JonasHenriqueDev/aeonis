#pragma once
// Miniaturas e durações dos vídeos, cacheadas em %LOCALAPPDATA%\Aeonis\thumbs com a mesma chave da
// versão C# (SHA-1 de caminho|tamanho|ticks), e ícones dos jogos em %LOCALAPPDATA%\Aeonis\icons.

#include <QString>

#include <optional>

namespace aeonis::MediaCache {

/// Chave do vídeo: muda se o arquivo for substituído ou alterado.
QString key(const QString &file, qint64 size, qint64 lastWriteTicks);
QString key(const QString &file);
/// LastWriteTimeUtc em ticks do .NET (100 ns desde 0001-01-01), lido do sistema de arquivos.
qint64 lastWriteTicks(const QString &file);

QString thumbPath(const QString &key);
bool hasThumb(const QString &key);
std::optional<double> duration(const QString &key);
/// Capa do player: o mesmo quadro da miniatura na resolução do vídeo (até 1920 px).
QString posterPath(const QString &key);
/// Gera a capa se ainda não existe; devolve o caminho (vazio se falhou). Bloqueante.
QString ensurePoster(const QString &video, const QString &key);
/// Gera a miniatura e grava a duração (uma abertura do vídeo para os dois).
std::optional<double> generate(const QString &video, const QString &key);

/// PNG do ícone do executável do jogo, para a barra lateral da biblioteca.
void saveGameIcon(const QString &game, const QString &exe);
QString gameIconPath(const QString &game);
/// PNG do campeão do League of Legends (id do Data Dragon, ex.: "MonkeyKing").
QString championIconPath(const QString &championId);

} // namespace aeonis::MediaCache
