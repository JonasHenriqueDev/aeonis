#pragma once
// Monitores na ordem usada pelas configurações: 0 = principal, 1.. = os demais.
// A captura acha a saída DXGI pelo HMONITOR (o índice DXGI nem sempre bate com essa ordem).

#include <QList>
#include <QString>

#include <windows.h>

namespace aeonis::Monitors {

struct Info {
    HMONITOR handle = nullptr;
    QString name;   // \\.\DISPLAY1
    int width = 0, height = 0;
    bool primary = false;
};

QList<Info> list();
/// Índice das configurações -> HMONITOR (o principal se o índice não existir).
HMONITOR byIndex(int index);
/// Monitor onde a janela está.
HMONITOR ofWindow(HWND hwnd);

} // namespace aeonis::Monitors
