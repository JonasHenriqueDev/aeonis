#include "WindowHost.h"
#include "AppIcon.h"
#include "core/Log.h"
#include "ui/Backend.h"

#include <QPixmapCache>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>

#include <windows.h>
#include <dwmapi.h>

namespace aeonis {

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
