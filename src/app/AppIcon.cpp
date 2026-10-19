#include "AppIcon.h"

namespace aeonis::AppIcon {

QIcon create(bool recording)
{
    // assets/icons, embutidos no executável (qrc).
    QIcon icon;
    const QString base = recording ? QStringLiteral(":/icons/aeonis-recording-%1.png") : QStringLiteral(":/icons/aeonis-%1.png");
    for (int size : {32, 64})
        icon.addFile(base.arg(size), QSize(size, size));
    if (!recording)
        icon.addFile(QStringLiteral(":/icons/aeonis-256.png"), QSize(256, 256));
    return icon;
}

} // namespace aeonis::AppIcon
