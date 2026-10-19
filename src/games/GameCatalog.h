#pragma once
// Jogos reconhecidos pelo nome do processo (lista embutida + jogos personalizados das configurações).

#include <QList>
#include <QString>
#include <QStringList>

namespace aeonis {

struct GameDef {
    QString name;
    QStringList processes; // nomes de processo sem ".exe"
    QString provider;      // fonte de destaques automáticos ("lol", "cs2") ou vazio
};

namespace GameCatalog {

QList<GameDef> builtIn();
/// "Nome=executavel.exe;outro.exe", um por linha (# comenta).
QList<GameDef> parseCustom(const QString &text);
/// Personalizados primeiro (podem sobrepor os embutidos).
QList<GameDef> all(const QString &customGames);
/// Nome amigável de um executável (ProductName/FileDescription da versão do arquivo; senão o nome do arquivo).
QString displayName(const QString &exePath);

} // namespace GameCatalog

} // namespace aeonis
