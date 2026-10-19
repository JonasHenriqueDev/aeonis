#include "UpdateSplash.h"
#include "AppIcon.h"
#include "AutoUpdater.h"
#include "core/Log.h"

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>

namespace aeonis {

UpdateSplash::UpdateSplash(AutoUpdater *updater, QObject *parent) : QObject(parent), m_updater(updater) {}

UpdateSplash::~UpdateSplash() { close(); }

void UpdateSplash::show()
{
    if (m_engine)
        return;
    m_engine = new QQmlApplicationEngine(this);
    m_engine->rootContext()->setContextProperty(QStringLiteral("Updater"), m_updater);
    m_engine->loadFromModule(QStringLiteral("Aeonis"), QStringLiteral("UpdateSplash"));
    auto *window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().value(0));
    if (!window) {
        Log::error(QStringLiteral("Falha ao carregar a tela de atualização (UpdateSplash.qml)."));
        close();
        return;
    }
    window->setIcon(AppIcon::create(false));
    window->show();
    window->raise();
    window->requestActivate();
}

void UpdateSplash::close()
{
    if (!m_engine)
        return;
    QQmlApplicationEngine *engine = m_engine;
    m_engine = nullptr;
    qDeleteAll(engine->rootObjects());
    delete engine;
}

} // namespace aeonis
