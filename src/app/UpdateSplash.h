#pragma once
// Janela de atualização no estilo do Discord: logo animada, estado e barra de progresso enquanto a
// versão nova é baixada e o instalador (silencioso) é iniciado. Só existe durante a atualização.

#include <QObject>

class QQmlApplicationEngine;

namespace aeonis {

class AutoUpdater;

class UpdateSplash : public QObject {
    Q_OBJECT
public:
    UpdateSplash(AutoUpdater *updater, QObject *parent = nullptr);
    ~UpdateSplash() override;

    void show();
    void close();
    bool isOpen() const { return m_engine != nullptr; }

private:
    AutoUpdater *m_updater;
    QQmlApplicationEngine *m_engine = nullptr;
};

} // namespace aeonis
