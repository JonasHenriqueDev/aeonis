#include "WindowHost.h"
#include "AppIcon.h"
#include "core/Log.h"
#include "ui/Backend.h"

#include <QGuiApplication>
#include <QPixmapCache>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>

#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

namespace aeonis {

namespace {

int frameThickness(HWND hwnd)
{
    const UINT dpi = GetDpiForWindow(hwnd);
    return GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

} // namespace

TitleBarFilter::TitleBarFilter(QQuickWindow *window)
    : m_window(window), m_hwnd(reinterpret_cast<HWND>(window->winId()))
{
    const HWND hwnd = m_hwnd;
    // Sem moldura para o Qt, mas com os estilos de uma janela normal para o Windows (snap, animações).
    SetWindowLongPtrW(hwnd, GWL_STYLE,
                      GetWindowLongPtrW(hwnd, GWL_STYLE) | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    const MARGINS shadow{0, 0, 1, 0}; // mantém a sombra do DWM
    DwmExtendFrameIntoClientArea(hwnd, &shadow);
    const int round = 2; // DWMWCP_ROUND (Windows 11; ignorado no 10)
    DwmSetWindowAttribute(hwnd, 33, &round, sizeof(round));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool TitleBarFilter::nativeEventFilter(const QByteArray &, void *message, qintptr *result)
{
    MSG *msg = static_cast<MSG *>(message);
    if (!m_hwnd || msg->hwnd != m_hwnd || !m_window)
        return false;
    const HWND hwnd = msg->hwnd;
    if (msg->message == WM_NCDESTROY) {
        m_hwnd = nullptr; // a janela nativa acabou; daqui em diante o filtro não faz nada
        return false;
    }

    if (msg->message == WM_NCCALCSIZE && msg->wParam) {
        // A área do cliente ocupa a janela toda. Maximizada, a janela pode passar da área de trabalho
        // pela espessura da borda: recorta para não esconder a barra nem cobrir a barra de tarefas.
        if (IsZoomed(hwnd)) {
            auto *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam);
            MONITORINFO mi{sizeof(mi)};
            if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
                IntersectRect(&params->rgrc[0], &params->rgrc[0], &mi.rcWork);
        }
        *result = 0;
        return true;
    }

    if (msg->message == WM_NCHITTEST) {
        POINT pt{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
        ScreenToClient(hwnd, &pt);
        RECT rc;
        GetClientRect(hwnd, &rc);
        const bool resizable = !IsZoomed(hwnd) && m_window->visibility() != QWindow::FullScreen;
        if (resizable) {
            const int f = frameThickness(hwnd);
            const bool left = pt.x < f, right = pt.x >= rc.right - f, top = pt.y < f, bottom = pt.y >= rc.bottom - f;
            if (top && left) { *result = HTTOPLEFT; return true; }
            if (top && right) { *result = HTTOPRIGHT; return true; }
            if (bottom && left) { *result = HTBOTTOMLEFT; return true; }
            if (bottom && right) { *result = HTBOTTOMRIGHT; return true; }
            if (left) { *result = HTLEFT; return true; }
            if (right) { *result = HTRIGHT; return true; }
            if (top) { *result = HTTOP; return true; }
            if (bottom) { *result = HTBOTTOM; return true; }
        }
        const qreal dpr = m_window->devicePixelRatio();
        const int barHeight = int(m_window->property("titleBarHeight").toReal() * dpr);
        const int buttons = int(m_window->property("titleBarButtonsWidth").toReal() * dpr);
        *result = pt.y < barHeight && pt.x < rc.right - buttons ? HTCAPTION : HTCLIENT;
        return true;
    }
    return false;
}

WindowHost::WindowHost(Backend *backend, QObject *parent) : QObject(parent), m_backend(backend) {}

WindowHost::~WindowHost() { destroy(); }

void WindowHost::show(const QString &page)
{
    if (m_engine && m_window) {
        emit m_backend->navigate(page);
        if (m_window->visibility() == QWindow::Minimized)
            m_window->showNormal();
        m_window->show();
        m_window->raise();
        m_window->requestActivate();
        return;
    }
    m_backend->setActive(true);
    m_engine = new QQmlApplicationEngine(this);
    m_engine->rootContext()->setContextProperty(QStringLiteral("App"), m_backend);
    m_engine->rootContext()->setContextProperty(QStringLiteral("InitialPage"), page);
    m_engine->loadFromModule(QStringLiteral("Aeonis"), QStringLiteral("Main"));
    m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().value(0));
    if (!m_window) {
        Log::error(QStringLiteral("Falha ao carregar a interface (Main.qml)."));
        destroy();
        return;
    }
    m_window->setIcon(AppIcon::create(false));
    m_titleBar = std::make_unique<TitleBarFilter>(m_window);
    qApp->installNativeEventFilter(m_titleBar.get());
    // Barra de título escura (Windows 10 20H1+ / 11).
    const HWND hwnd = reinterpret_cast<HWND>(m_window->winId());
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark))))
        DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
    connect(m_window, &QQuickWindow::closing, this, [this] {
        // Destrói depois de o evento de fechar terminar.
        QTimer::singleShot(0, this, &WindowHost::destroy);
    });
    m_window->show();
    m_window->raise();
    m_window->requestActivate();
}

void WindowHost::close()
{
    if (m_titleBar) {
        qApp->removeNativeEventFilter(m_titleBar.get());
        m_titleBar.reset();
    }
    if (m_window)
        m_window->close();
    destroy();
}

void WindowHost::destroy()
{
    if (!m_engine)
        return;
    m_backend->setActive(false);
    QQmlApplicationEngine *engine = m_engine;
    if (m_titleBar) {
        qApp->removeNativeEventFilter(m_titleBar.get());
        m_titleBar.reset();
    }
    m_engine = nullptr;
    m_window = nullptr;
    // Objetos da janela primeiro, enquanto o contexto (App) ainda existe.
    qDeleteAll(engine->rootObjects());
    delete engine;
    QPixmapCache::clear();
    // A liberação dos recursos gráficos termina no próximo ciclo; aí devolve a memória.
    QTimer::singleShot(500, [] { releaseMemory(); });
}

void WindowHost::releaseMemory()
{
    SetProcessWorkingSetSize(GetCurrentProcess(), SIZE_T(-1), SIZE_T(-1));
}

} // namespace aeonis
