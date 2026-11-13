#include "DeadlockSetup.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

namespace aeonis::DeadlockSetup {

namespace {

QString readText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > (64 << 20))
        return {};
    return QString::fromUtf8(f.readAll());
}

QString unescape(QString v)
{
    v.replace(QLatin1String("\\\""), QLatin1String("\""));
    v.replace(QLatin1String("\\\\"), QLatin1String("\\"));
    return v;
}

// Valor de `"chave"  "valor"` dentro de `text` (VDF da Steam), ou vazio.
QString vdfValue(const QString &text, const QString &key)
{
    const QRegularExpression re(QLatin1Char('"') + QRegularExpression::escape(key)
                                + QStringLiteral(R"re("\s+"((?:[^"\\]|\\.)*)")re"),
                                QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(text);
    return m.hasMatch() ? unescape(m.captured(1)) : QString();
}

// Conteúdo entre a `{` em `open` e a `}` correspondente (ignorando chaves dentro de aspas).
QString block(const QString &text, qsizetype open)
{
    int depth = 0;
    bool quoted = false;
    for (qsizetype i = open; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (quoted) {
            if (c == QLatin1Char('\\'))
                ++i;
            else if (c == QLatin1Char('"'))
                quoted = false;
        } else if (c == QLatin1Char('"')) {
            quoted = true;
        } else if (c == QLatin1Char('{')) {
            ++depth;
        } else if (c == QLatin1Char('}') && --depth == 0) {
            return text.mid(open + 1, i - open - 1);
        }
    }
    return {};
}

QString steamPath()
{
    const QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"), QSettings::NativeFormat);
    return QDir::fromNativeSeparators(reg.value(QStringLiteral("SteamPath")).toString());
}

} // namespace

bool hasCondebug(const QString &launchOptions)
{
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    for (const QString &token : launchOptions.split(ws, Qt::SkipEmptyParts))
        if (token.compare(QLatin1String("-condebug"), Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

AppConfig appConfig(const QString &vdf)
{
    static const QRegularExpression keyRe(QStringLiteral(R"re("%1"\s*\{)re").arg(QLatin1String(AppId)));
    AppConfig best;
    for (auto it = keyRe.globalMatch(vdf); it.hasNext();) {
        const auto m = it.next();
        const QString body = block(vdf, m.capturedEnd() - 1);
        AppConfig c;
        c.found = true;
        c.launchOptions = vdfValue(body, QStringLiteral("LaunchOptions"));
        c.lastPlayed = vdfValue(body, QStringLiteral("LastPlayed")).toLongLong();
        // O app aparece em várias seções: vale a que tem LaunchOptions; entre iguais, a de LastPlayed maior.
        const bool opts = !c.launchOptions.isEmpty(), bestOpts = !best.launchOptions.isEmpty();
        if (!best.found || (opts && !bestOpts) || (opts == bestOpts && c.lastPlayed > best.lastPlayed))
            best = c;
    }
    return best;
}

State check()
{
    State s;
    const QString steam = steamPath();
    if (steam.isEmpty())
        return s;

    QStringList libraries{steam};
    static const QRegularExpression pathRe(QStringLiteral(R"re("path"\s+"((?:[^"\\]|\\.)*)")re"));
    for (auto it = pathRe.globalMatch(readText(steam + QStringLiteral("/steamapps/libraryfolders.vdf"))); it.hasNext();)
        libraries << QDir::fromNativeSeparators(unescape(it.next().captured(1)));
    for (const QString &lib : libraries)
        if (QFileInfo::exists(lib + QStringLiteral("/steamapps/appmanifest_%1.acf").arg(QLatin1String(AppId))))
            s.installed = true;

    // Várias contas no PC: vale a que jogou Deadlock por último.
    AppConfig best;
    const QDir users(steam + QStringLiteral("/userdata"));
    for (const QString &id : users.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const AppConfig c = appConfig(readText(users.filePath(id + QStringLiteral("/config/localconfig.vdf"))));
        if (c.found && (!best.found || c.lastPlayed > best.lastPlayed))
            best = c;
    }
    s.known = best.found;
    s.launchOptions = best.launchOptions;
    s.condebug = hasCondebug(best.launchOptions);
    return s;
}

} // namespace aeonis::DeadlockSetup
