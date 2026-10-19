#pragma once
// Esquema da tela de configurações (como o FieldDto da versão C#): cada campo com categoria, rótulo,
// descrição, tipo de controle e opções. O QML gera o formulário a partir daqui.

#include "core/Settings.h"

#include <QVariantList>
#include <QVariantMap>

namespace aeonis::SettingsSchema {

QVariantList fields(const Settings &s);
/// Aplica os valores alterados ({ chave: valor }). `error` explica um valor inválido (ex.: atalho).
bool apply(Settings &s, const QVariantMap &values, QString *error);

} // namespace aeonis::SettingsSchema
