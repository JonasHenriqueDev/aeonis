#include "Monitors.h"

namespace aeonis::Monitors {

namespace {

BOOL CALLBACK collect(HMONITOR monitor, HDC, LPRECT, LPARAM param)
{
    auto *out = reinterpret_cast<QList<Info> *>(param);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi))
        return TRUE;
    Info info;
    info.handle = monitor;
    info.name = QString::fromWCharArray(mi.szDevice);
    info.width = mi.rcMonitor.right - mi.rcMonitor.left;
    info.height = mi.rcMonitor.bottom - mi.rcMonitor.top;
    info.primary = mi.dwFlags & MONITORINFOF_PRIMARY;
    if (info.primary)
        out->prepend(info);
    else
        out->append(info);
    return TRUE;
}

} // namespace

QList<Info> list()
{
    QList<Info> out;
    EnumDisplayMonitors(nullptr, nullptr, collect, reinterpret_cast<LPARAM>(&out));
    // EnumDisplayMonitors pode listar o principal depois de outro: garante o principal em 0.
    for (int i = 1; i < out.size(); ++i)
        if (out[i].primary)
            out.move(i, 0);
    return out;
}

HMONITOR byIndex(int index)
{
    const QList<Info> all = list();
    if (index >= 0 && index < all.size())
        return all[index].handle;
    return MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
}

HMONITOR ofWindow(HWND hwnd)
{
    if (!hwnd)
        return MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    return MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
}

} // namespace aeonis::Monitors
