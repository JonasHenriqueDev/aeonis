#pragma once
// Monta a biblioteca no formato dia -> partida -> clipes. A partida vem do histórico
// (library.jsonl); vídeos sem histórico (antigos, importados) são agrupados por jogo e por
// proximidade no tempo. O resultado vai para o QML como listas/mapas (QVariant).

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace aeonis::LibraryModel {

struct Result {
    QVariantMap data;           // { matches, games, totalBytes, unsavedBytes, outputDir }
    QVariantList missingThumbs; // [{ id, path }] para gerar em segundo plano
};

/// Varre a pasta dos vídeos (bloqueante: chame fora da UI).
Result build(const QString &outputDir);

QString labelFromName(const QString &baseName);
QString kindFromLabel(const QString &label);

} // namespace aeonis::LibraryModel
