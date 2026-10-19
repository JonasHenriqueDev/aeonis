#pragma once
// Atalhos globais via RegisterHotKey (funcionam com o jogo em foco, sem hook de teclado),
// recebidos por uma janela só de mensagens.

#include <QHash>
#include <QString>

#include <functional>

#include <windows.h>

namespace aeonis {

class Hotkeys {
public:
    Hotkeys();
    ~Hotkeys();

    bool add(const QString &combo, std::function<void()> action, QString *error);
    void clear();

    /// "Alt+F10", "Ctrl+Shift+S" -> modificadores MOD_* e tecla virtual.
    static bool parse(const QString &combo, UINT *mods, UINT *vk);

private:
    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    HWND m_hwnd = nullptr;
    QHash<int, std::function<void()>> m_actions;
    int m_nextId = 1;
};

} // namespace aeonis
