#include "GameWatcher.h"
#include "capture/Monitors.h"
#include "core/Log.h"

#include <QDateTime>
#include <QHash>

#include <tlhelp32.h>

namespace aeonis {

namespace {

constexpr qint64 WindowGoneMs = 60'000;

struct WindowSearch {
    DWORD pid = 0;
    HWND found = nullptr;
};

BOOL CALLBACK findMainWindow(HWND hwnd, LPARAM param)
{
    auto *s = reinterpret_cast<WindowSearch *>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == s->pid && IsWindowVisible(hwnd) && !GetWindow(hwnd, GW_OWNER)) {
        s->found = hwnd;
        return FALSE;
    }
    return TRUE;
}

HWND mainWindowOf(DWORD pid)
{
    WindowSearch s{pid};
    EnumWindows(findMainWindow, reinterpret_cast<LPARAM>(&s));
    return s.found;
}

QString exePathOf(DWORD pid)
{
    QString path;
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t buf[MAX_PATH * 2];
        DWORD size = DWORD(std::size(buf));
        if (QueryFullProcessImageNameW(h, 0, buf, &size))
            path = QString::fromWCharArray(buf, int(size));
        CloseHandle(h);
    }
    return path;
}

QString baseName(const wchar_t *exe)
{
    QString n = QString::fromWCharArray(exe);
    if (n.endsWith(QLatin1String(".exe"), Qt::CaseInsensitive))
        n.chop(4);
    return n;
}

} // namespace

GameWatcher::GameWatcher(std::function<QList<GameDef>()> games, QObject *parent)
    : QObject(parent), m_games(std::move(games))
{
    m_timer.setInterval(3000);
    connect(&m_timer, &QTimer::timeout, this, &GameWatcher::tick);
}

void GameWatcher::start()
{
    m_timer.start();
    tick();
}

void GameWatcher::stop() { m_timer.stop(); }

bool GameWatcher::isAlive(DWORD pid, const QString &name)
{
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid)) {
        DWORD code = 0;
        const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
        CloseHandle(h);
        return alive;
    }
    // Processos protegidos por anti-cheat podem negar o handle; existir na lista já basta.
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return true;
    bool found = false;
    PROCESSENTRY32W pe{sizeof(pe)};
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe))
        found = pe.th32ProcessID == pid && baseName(pe.szExeFile).compare(name, Qt::CaseInsensitive) == 0;
    CloseHandle(snap);
    return found;
}

void GameWatcher::tick()
{
    if (m_current) {
        bool ended = !isAlive(m_currentPid, m_currentName);
        if (!ended) {
            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (mainWindowOf(m_currentPid)) {
                m_hadWindow = true;
                m_noWindowSince = 0;
            } else if (m_hadWindow) {
                if (!m_noWindowSince)
                    m_noWindowSince = now;
                else if (now - m_noWindowSince >= WindowGoneMs) {
                    Log::info(QStringLiteral("%1 sem janela há %2 s (processo preso em segundo plano): sessão encerrada.")
                                  .arg(m_current->name).arg(WindowGoneMs / 1000));
                    m_ignoredPid = m_currentPid;
                    ended = true;
                }
            }
        }
        if (ended) {
            const GameDef g = *m_current;
            m_current.reset();
            Log::info(QStringLiteral("Jogo encerrado: %1").arg(g.name));
            emit gameStopped(g);
        }
        return;
    }

    if (m_pending) {
        if (!isAlive(m_pendingPid, m_pendingName)) {
            m_pending.reset();
            return;
        }
        const HWND hwnd = mainWindowOf(m_pendingPid);
        if (!hwnd && QDateTime::currentMSecsSinceEpoch() - m_pendingSince < 20'000)
            return;
        m_current = m_pending;
        m_currentPid = m_pendingPid;
        m_currentName = m_pendingName;
        m_hadWindow = hwnd != nullptr;
        m_noWindowSince = 0;
        m_pending.reset();
        Log::info(QStringLiteral("Jogo detectado: %1 (pid %2)").arg(m_current->name).arg(m_currentPid));
        emit gameStarted(*m_current, Monitors::ofWindow(hwnd), exePathOf(m_currentPid));
        return;
    }

    QHash<QString, GameDef> map;
    for (const GameDef &g : m_games())
        for (const QString &p : g.processes)
            if (!map.contains(p.toLower()))
                map.insert(p.toLower(), g);

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return;
    PROCESSENTRY32W pe{sizeof(pe)};
    bool ignoredAlive = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (m_ignoredPid && pe.th32ProcessID == m_ignoredPid) {
            ignoredAlive = true;
            continue;
        }
        const QString name = baseName(pe.szExeFile);
        const auto it = map.constFind(name.toLower());
        if (it != map.constEnd()) {
            m_pending = *it;
            m_pendingPid = pe.th32ProcessID;
            m_pendingName = name;
            m_pendingSince = QDateTime::currentMSecsSinceEpoch();
            break;
        }
    }
    CloseHandle(snap);
    if (!ignoredAlive && !m_pending)
        m_ignoredPid = 0;
    if (m_pending)
        tick(); // a janela já pode estar aberta
}

} // namespace aeonis
