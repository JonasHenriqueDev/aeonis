#pragma once
// Ícone na bandeja: status, salvar clipe, gravação, abrir a janela, pasta, configurações, log e sair.
// Sem a janela aberta, o Aeonis é só isto (e a captura, quando há jogo).

#include <QObject>
#include <QSystemTrayIcon>

class QAction;
class QMenu;

namespace aeonis {

class TrayIcon : public QObject {
    Q_OBJECT
public:
    explicit TrayIcon(QObject *parent = nullptr);
    ~TrayIcon() override;

    struct State {
        QString status;        // "Aguardando jogo…", "● Gravando: X", ...
        bool canSave = false;
        bool recording = false;
        QString hotkeySave, hotkeyRecord;
    };
    void update(const State &state);
    void notify(const QString &title, const QString &text,
                QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information);
    void hide();

signals:
    void openRequested();
    void settingsRequested();
    void saveClipRequested();
    void toggleRecordingRequested();
    void openFolderRequested();
    void openLogRequested();
    void quitRequested();

private:
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
    QAction *m_status = nullptr, *m_save = nullptr, *m_record = nullptr;
    QIcon m_idle, m_recordingIcon;
    bool m_recording = false;
};

} // namespace aeonis
