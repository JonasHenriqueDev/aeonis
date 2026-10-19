#include "Hotkeys.h"

#include <QStringList>

namespace aeonis {

namespace {

const wchar_t *ClassName = L"AeonisHotkeys";

/// Nomes de tecla do enum Keys do .NET (formato das configurações da versão C#).
UINT keyFromName(const QString &raw)
{
    const QString k = raw.trimmed();
    const QString l = k.toLower();
    if (k.size() == 1) {
        const QChar c = k[0].toUpper();
        if ((c >= QLatin1Char('A') && c <= QLatin1Char('Z')) || (c >= QLatin1Char('0') && c <= QLatin1Char('9')))
            return c.unicode();
    }
    if (l.size() == 2 && l[0] == QLatin1Char('d') && l[1].isDigit())
        return l[1].unicode(); // D0..D9
    if (l.startsWith(QLatin1Char('f'))) {
        bool ok = false;
        const int n = l.mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 24)
            return VK_F1 + UINT(n - 1);
    }
    if (l.startsWith(QLatin1String("numpad"))) {
        bool ok = false;
        const int n = l.mid(6).toInt(&ok);
        if (ok && n >= 0 && n <= 9)
            return VK_NUMPAD0 + UINT(n);
    }
    static const QHash<QString, UINT> named = {
        {QStringLiteral("insert"), VK_INSERT}, {QStringLiteral("delete"), VK_DELETE}, {QStringLiteral("home"), VK_HOME},
        {QStringLiteral("end"), VK_END}, {QStringLiteral("pageup"), VK_PRIOR}, {QStringLiteral("prior"), VK_PRIOR},
        {QStringLiteral("pagedown"), VK_NEXT}, {QStringLiteral("next"), VK_NEXT}, {QStringLiteral("pause"), VK_PAUSE},
        {QStringLiteral("space"), VK_SPACE}, {QStringLiteral("printscreen"), VK_SNAPSHOT},
        {QStringLiteral("snapshot"), VK_SNAPSHOT}, {QStringLiteral("tab"), VK_TAB}, {QStringLiteral("up"), VK_UP},
        {QStringLiteral("down"), VK_DOWN}, {QStringLiteral("left"), VK_LEFT}, {QStringLiteral("right"), VK_RIGHT},
        {QStringLiteral("multiply"), VK_MULTIPLY}, {QStringLiteral("add"), VK_ADD},
        {QStringLiteral("subtract"), VK_SUBTRACT}, {QStringLiteral("divide"), VK_DIVIDE},
        {QStringLiteral("decimal"), VK_DECIMAL}, {QStringLiteral("scroll"), VK_SCROLL},
        {QStringLiteral("oemtilde"), VK_OEM_3}, {QStringLiteral("oemminus"), VK_OEM_MINUS},
        {QStringLiteral("oemplus"), VK_OEM_PLUS},
    };
    return named.value(l, 0);
}

} // namespace

Hotkeys::Hotkeys()
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = &Hotkeys::proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = ClassName;
    RegisterClassW(&wc);
    m_hwnd = CreateWindowExW(0, ClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
}

Hotkeys::~Hotkeys()
{
    clear();
    if (m_hwnd)
        DestroyWindow(m_hwnd);
}

LRESULT CALLBACK Hotkeys::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_HOTKEY) {
        if (auto *self = reinterpret_cast<Hotkeys *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
            const auto action = self->m_actions.value(int(wp));
            if (action)
                action();
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool Hotkeys::parse(const QString &combo, UINT *mods, UINT *vk)
{
    *mods = 0;
    *vk = 0;
    for (const QString &part : combo.split(QLatin1Char('+'), Qt::SkipEmptyParts)) {
        const QString p = part.trimmed().toLower();
        if (p == QLatin1String("ctrl") || p == QLatin1String("control")) *mods |= MOD_CONTROL;
        else if (p == QLatin1String("alt")) *mods |= MOD_ALT;
        else if (p == QLatin1String("shift")) *mods |= MOD_SHIFT;
        else if (p == QLatin1String("win")) *mods |= MOD_WIN;
        else {
            const UINT key = keyFromName(part);
            if (!key)
                return false;
            *vk = key;
        }
    }
    return *vk != 0;
}

bool Hotkeys::add(const QString &combo, std::function<void()> action, QString *error)
{
    UINT mods = 0, vk = 0;
    if (!parse(combo, &mods, &vk)) {
        *error = QStringLiteral("Atalho inválido: \"%1\"").arg(combo);
        return false;
    }
    const int id = m_nextId++;
    if (!RegisterHotKey(m_hwnd, id, mods | MOD_NOREPEAT, vk)) {
        *error = QStringLiteral("O atalho %1 já está em uso por outro programa.").arg(combo);
        return false;
    }
    m_actions.insert(id, std::move(action));
    return true;
}

void Hotkeys::clear()
{
    for (auto it = m_actions.cbegin(); it != m_actions.cend(); ++it)
        UnregisterHotKey(m_hwnd, it.key());
    m_actions.clear();
}

} // namespace aeonis
