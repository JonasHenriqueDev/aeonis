#pragma once
// Deadlock: as partidas só são detectadas com a opção de inicialização -condebug (o jogo passa a escrever
// o console.log que o DeadlockEvents lê). Isto confere, nos arquivos da Steam, se o jogo está instalado e
// se a opção está nas "Opções de inicialização", para a interface avisar e mostrar como ativar.

#include <QString>

namespace aeonis::DeadlockSetup {

constexpr const char *AppId = "1422450";

struct State {
    bool installed = false;    // está em alguma biblioteca da Steam
    bool known = false;        // achou as opções de inicialização da conta que joga
    bool condebug = false;     // -condebug está nelas
    QString launchOptions;
};

/// Lê o registro e os .vdf da Steam (rápido: poucos arquivos pequenos).
State check();

/// Bloco do app no localconfig.vdf de uma conta: { LaunchOptions, LastPlayed }. `found` = o bloco existe.
struct AppConfig {
    bool found = false;
    QString launchOptions;
    qint64 lastPlayed = 0;
};
AppConfig appConfig(const QString &localConfigVdf);
bool hasCondebug(const QString &launchOptions);

} // namespace aeonis::DeadlockSetup
