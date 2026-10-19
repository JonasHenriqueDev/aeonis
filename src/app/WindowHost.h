#pragma once
// A janela principal só existe enquanto está aberta: o motor QML é criado ao abrir e destruído ao
// fechar, e a memória volta para o Windows (SetProcessWorkingSetSize). Durante o jogo, com a
// janela fechada, o custo da interface é praticamente zero.

#include <QObject>
#include <QPointer>

class QQmlApplicationEngine;
class QQuickWindow;

namespace aeonis {

class Backend;

class WindowHost : public QObject {
    Q_OBJECT
public:
    WindowHost(Backend *backend, QObject *parent = nullptr);
    ~WindowHost() override;

    void show(const QString &page);
    void close();
    bool isOpen() const { return m_engine != nullptr; }

private:
    void destroy();
    static void releaseMemory();

    Backend *m_backend;
    QQmlApplicationEngine *m_engine = nullptr;
    QPointer<QQuickWindow> m_window;
};

} // namespace aeonis
