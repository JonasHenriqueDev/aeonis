#include "TrayIcon.h"
#include "AppIcon.h"

#include <QAction>
#include <QMenu>

namespace aeonis {

TrayIcon::TrayIcon(QObject *parent)
    : QObject(parent), m_idle(AppIcon::create(false)), m_recordingIcon(AppIcon::create(true))
{
    m_tray = new QSystemTrayIcon(m_idle, this);
    m_menu = new QMenu;
    m_status = m_menu->addAction(QStringLiteral("Aguardando jogo…"));
    m_status->setEnabled(false);
    m_menu->addSeparator();
    m_save = m_menu->addAction(QStringLiteral("Salvar clipe agora"));
    m_record = m_menu->addAction(QStringLiteral("Iniciar gravação"));
    m_menu->addSeparator();
    QAction *open = m_menu->addAction(QStringLiteral("Abrir Aeonis"));
    QFont bold = open->font();
    bold.setBold(true);
    open->setFont(bold);
    QAction *folder = m_menu->addAction(QStringLiteral("Abrir pasta dos vídeos"));
    QAction *settings = m_menu->addAction(QStringLiteral("Configurações"));
    QAction *log = m_menu->addAction(QStringLiteral("Abrir log"));
    m_menu->addSeparator();
    QAction *quit = m_menu->addAction(QStringLiteral("Sair"));
    m_tray->setContextMenu(m_menu);

    connect(m_save, &QAction::triggered, this, &TrayIcon::saveClipRequested);
    connect(m_record, &QAction::triggered, this, &TrayIcon::toggleRecordingRequested);
    connect(open, &QAction::triggered, this, &TrayIcon::openRequested);
    connect(folder, &QAction::triggered, this, &TrayIcon::openFolderRequested);
    connect(settings, &QAction::triggered, this, &TrayIcon::settingsRequested);
    connect(log, &QAction::triggered, this, &TrayIcon::openLogRequested);
    connect(quit, &QAction::triggered, this, &TrayIcon::quitRequested);
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, &TrayIcon::openRequested);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            emit openRequested();
    });
    m_tray->setToolTip(QStringLiteral("Aeonis"));
    m_tray->show();
}

TrayIcon::~TrayIcon()
{
    m_tray->hide();
    delete m_menu;
}

void TrayIcon::update(const State &s)
{
    m_status->setText(s.status);
    m_save->setText(QStringLiteral("Salvar clipe agora (%1)").arg(s.hotkeySave));
    m_save->setEnabled(s.canSave);
    m_record->setText((s.recording ? QStringLiteral("Parar gravação") : QStringLiteral("Iniciar gravação"))
                      + QStringLiteral(" (%1)").arg(s.hotkeyRecord));
    if (s.recording != m_recording) {
        m_recording = s.recording;
        m_tray->setIcon(m_recording ? m_recordingIcon : m_idle);
    }
    QString tip = QStringLiteral("Aeonis — ") + s.status;
    if (tip.size() > 127)
        tip.truncate(127);
    m_tray->setToolTip(tip);
}

void TrayIcon::notify(const QString &title, const QString &text, QSystemTrayIcon::MessageIcon icon)
{
    if (m_tray->isVisible() && QSystemTrayIcon::supportsMessages())
        m_tray->showMessage(title, text, icon, 4000);
}

void TrayIcon::hide() { m_tray->hide(); }

} // namespace aeonis
