#pragma once
// A janela principal só existe enquanto está aberta: o motor QML é criado ao abrir e destruído ao
// fechar, e a memória volta para o Windows (SetProcessWorkingSetSize). Durante o jogo, com a
// janela fechada, o custo da interface é praticamente zero.

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>

#include <memory>

#include <windows.h>

class QQmlApplicationEngine;
class QQuickWindow;

namespace aeonis {

class Backend;

/// Barra de título própria (TitleBar.qml) mantendo o comportamento nativo do Windows: a área do
/// cliente cobre a janela inteira (WM_NCCALCSIZE) e o WM_NCHITTEST devolve bordas de redimensionar e
/// "caption" fora dos botões, então arrastar, Aero Snap, duplo clique e o menu do sistema continuam.
/// A altura da barra e a largura dos botões vêm das propriedades da janela QML.
class TitleBarFilter : public QAbstractNativeEventFilter {
public:
    explicit TitleBarFilter(QQuickWindow *window);
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    QPointer<QQuickWindow> m_window;
    // HWND guardado na criação: o filtro nunca chama winId(), que recriaria a janela nativa enquanto
    // ela é destruída ao fechar (milhares de CreateWindowEx falhando, até derrubar o app na 1.1.0/1.1.1).
    HWND m_hwnd = nullptr;
};

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
    std::unique_ptr<TitleBarFilter> m_titleBar;
};

} // namespace aeonis
