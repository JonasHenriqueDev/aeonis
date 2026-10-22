#include "GameCatalog.h"

#include <QFileInfo>
#include <QRegularExpression>

#include <vector>

#include <windows.h>

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
        {QStringLiteral("Deadlock"), {QStringLiteral("deadlock"), QStringLiteral("project8")}, {}}, // project8 = nome dos testes fechados
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

QString displayName(const QString &exePath)
{
    const QString fallback = QFileInfo(exePath).completeBaseName();
    const std::wstring path = QFileInfo(exePath).absoluteFilePath().replace(QLatin1Char('/'), QLatin1Char('\\')).toStdWString();
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0)
        return fallback;
    std::vector<char> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()))
        return fallback;
    struct Translation { WORD language, codePage; } *tr = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void **>(&tr), &len) || len < sizeof(Translation))
        return fallback;
    for (const wchar_t *key : {L"ProductName", L"FileDescription"}) {
        const QString query = QStringLiteral("\\StringFileInfo\\%1%2\\%3")
                                  .arg(tr->language, 4, 16, QLatin1Char('0')).arg(tr->codePage, 4, 16, QLatin1Char('0'))
                                  .arg(QString::fromWCharArray(key));
        wchar_t *value = nullptr;
        if (VerQueryValueW(data.data(), reinterpret_cast<const wchar_t *>(query.utf16()), reinterpret_cast<void **>(&value), &len) && len > 1) {
            QString name = QString::fromWCharArray(value).trimmed();
            name.remove(QRegularExpression(QStringLiteral("[=;\r\n#]"))); // não quebra o formato Nome=exe
            // Nomes genéricos de motores/launchers não ajudam.
            static const QRegularExpression generic(QStringLiteral("^(BootstrapPackagedGame|UnrealGame|Unity|Launcher|Game)$"), QRegularExpression::CaseInsensitiveOption);
            if (!name.isEmpty() && !generic.match(name).hasMatch() && name.size() <= 60)
                return name;
        }
    }
    return fallback;
}

QList<GameDef> all(const QString &customGames)
{
    QList<GameDef> list = parseCustom(customGames);
    list.append(builtIn());
    return list;
}

} // namespace aeonis::GameCatalog
