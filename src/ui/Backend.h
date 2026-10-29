#pragma once
// O objeto "App" visto pelo QML: biblioteca, status, configurações e as ações da janela
// (mostrar na pasta, excluir, cortar, salvar ★, configurações). Os dados da biblioteca só existem
// enquanto a janela está aberta.

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>

namespace aeonis {

class App;
class AutoUpdater;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap library READ library NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList settingsFields READ settingsFields NOTIFY settingsChanged)
    Q_PROPERTY(QObject *updater READ updater CONSTANT)
    Q_PROPERTY(QObject *youtube READ youtube CONSTANT)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
public:
    Backend(App *app, QObject *parent = nullptr);

    QVariantMap status() const;
    QVariantMap library() const { return m_library; }
    QVariantList settingsFields() const;
    QObject *updater() const;
    QObject *youtube() const;
    bool loading() const { return m_loading; }

    /// Janela abriu/fechou: carrega ou descarta os dados da biblioteca.
    void setActive(bool active);

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void reveal(const QString &path);
    Q_INVOKABLE void openExternal(const QString &path);
    Q_INVOKABLE void openFolder();
    Q_INVOKABLE void openLog();
    Q_INVOKABLE void deleteClips(const QStringList &paths);
    /// `compress`: re-encoda para um arquivo menor (Media::trim).
    Q_INVOKABLE void trim(const QString &path, double start, double end, bool compress = false);
    /// Clipes sugeridos: um clipe por grupo de eventos escolhidos ({ t, label }) do vídeo, com os segundos
    /// antes/depois das configurações. Responde com eventClipsDone.
    Q_INVOKABLE void clipsFromEvents(const QString &path, const QVariantList &events, bool compress = false);
    Q_INVOKABLE void setSaved(const QStringList &paths, bool saved);
    Q_INVOKABLE bool saveSettings(const QVariantMap &values);
    /// Envia o vídeo ao YouTube. `video`: title, description, tags (separadas por vírgula),
    /// privacy (public | unlisted | private) e madeForKids.
    Q_INVOKABLE void uploadToYouTube(const QString &path, const QVariantMap &video);
    Q_INVOKABLE void copyText(const QString &text);
    /// Abre um link do YouTube no navegador.
    Q_INVOKABLE void openYouTube(const QString &url);
    Q_INVOKABLE void saveClipNow();
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE QString folderUrl(const QString &path) const;
    Q_INVOKABLE QString pathFromUrl(const QUrl &url) const;
    /// Jogo a partir de um executável escolhido no Explorer: { name, process, icon } (vazio se inválido).
    Q_INVOKABLE QVariantMap gameFromExe(const QUrl &url) const;
    /// Ícone do jogo: o embutido do catálogo (qrc:/games/...), senão o extraído do executável, senão vazio.
    Q_INVOKABLE QString gameIconUrl(const QString &game) const;
    /// Gera (em segundo plano) a capa em alta resolução do vídeo; responde com posterReady.
    Q_INVOKABLE void requestPoster(const QString &id, const QString &path);

signals:
    void statusChanged();
    void libraryChanged();
    void settingsChanged();
    void loadingChanged();
    void toast(const QString &text, const QString &kind);
    void navigate(const QString &page);
    void thumbReady(const QString &id, const QString &url, double duration);
    void posterReady(const QString &id, const QString &url);
    void trimDone(bool ok, const QString &path);
    void eventClipsDone(int saved);

private:
    QString safeVideo(const QString &path) const;
    void generateThumbs(const QVariantList &missing);

    App *m_app;
    QVariantMap m_library;
    bool m_active = false;
    bool m_loading = false;
    bool m_reloadPending = false;
    std::atomic<int> m_thumbGeneration{0};
};

} // namespace aeonis
