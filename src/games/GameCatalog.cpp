#include "GameCatalog.h"

namespace aeonis::GameCatalog {

QList<GameDef> builtIn()
{
    static const QList<GameDef> list = {
        {QStringLiteral("League of Legends"), {QStringLiteral("League of Legends")}, QStringLiteral("lol")},
        {QStringLiteral("Counter-Strike 2"), {QStringLiteral("cs2")}, QStringLiteral("cs2")},
        {QStringLiteral("Valorant"), {QStringLiteral("VALORANT-Win64-Shipping")}, {}},
        {QStringLiteral("Fortnite"), {QStringLiteral("FortniteClient-Win64-Shipping")}, {}},
        {QStringLiteral("Apex Legends"), {QStringLiteral("r5apex"), QStringLiteral("r5apex_dx12")}, {}},
        {QStringLiteral("Overwatch 2"), {QStringLiteral("Overwatch")}, {}},
        {QStringLiteral("Rocket League"), {QStringLiteral("RocketLeague")}, {}},
        {QStringLiteral("Dota 2"), {QStringLiteral("dota2")}, {}},
        {QStringLiteral("Rainbow Six Siege"), {QStringLiteral("RainbowSix"), QStringLiteral("RainbowSix_Vulkan")}, {}},
        {QStringLiteral("Call of Duty"), {QStringLiteral("cod")}, {}},
        {QStringLiteral("PUBG"), {QStringLiteral("TslGame")}, {}},
        {QStringLiteral("GTA V"), {QStringLiteral("GTA5"), QStringLiteral("GTA5_Enhanced")}, {}},
        {QStringLiteral("Marvel Rivals"), {QStringLiteral("Marvel-Win64-Shipping")}, {}},
        {QStringLiteral("Deadlock"), {QStringLiteral("project8")}, {}},
        {QStringLiteral("Rust"), {QStringLiteral("RustClient")}, {}},
        {QStringLiteral("Escape from Tarkov"), {QStringLiteral("EscapeFromTarkov")}, {}},
        {QStringLiteral("The Finals"), {QStringLiteral("Discovery")}, {}},
        {QStringLiteral("Elden Ring"), {QStringLiteral("eldenring")}, {}},
        {QStringLiteral("Dead by Daylight"), {QStringLiteral("DeadByDaylight-Win64-Shipping")}, {}},
        {QStringLiteral("Roblox"), {QStringLiteral("RobloxPlayerBeta")}, {}},
        {QStringLiteral("Minecraft"), {QStringLiteral("Minecraft.Windows")}, {}},
        {QStringLiteral("Path of Exile 2"), {QStringLiteral("PathOfExile"), QStringLiteral("PathOfExileSteam"), QStringLiteral("PathOfExile_x64Steam")}, {}},
        {QStringLiteral("Brawlhalla"), {QStringLiteral("Brawlhalla")}, {}},
    };
    return list;
}

QList<GameDef> parseCustom(const QString &text)
{
    QList<GameDef> out;
    for (const QString &raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        const QString name = eq > 0 ? line.left(eq).trimmed() : line;
        QStringList exes;
        for (QString e : (eq > 0 ? line.mid(eq + 1) : line).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            e = e.trimmed();
            if (e.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
                e.chop(4);
            if (!e.isEmpty())
                exes << e;
        }
        if (!exes.isEmpty())
            out.append({name, exes, {}});
    }
    return out;
}

QList<GameDef> all(const QString &customGames)
{
    QList<GameDef> list = parseCustom(customGames);
    list.append(builtIn());
    return list;
}

} // namespace aeonis::GameCatalog
