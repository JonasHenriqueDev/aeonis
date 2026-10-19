#pragma once
// Pastas e arquivos do Aeonis. Os dados ficam em %LOCALAPPDATA%\Aeonis (os mesmos da versão C#);
// AEONIS_DATA permite outra pasta (testes, modo portátil).

#include <QString>

namespace aeonis::Paths {

QString appData();
QString settingsFile();
QString savedFile();
QString logFile();
QString buffer();
QString thumbs();
QString icons();
QString libraryFile();
/// %USERPROFILE%\Videos\Aeonis (pasta padrão dos vídeos).
QString defaultVideos();
/// Pasta do executável.
QString appDir();
/// Cria as pastas de dados (chamado uma vez na inicialização).
void ensure();

} // namespace aeonis::Paths
