#include "FontProbe.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QFont>

namespace NexusAI {

FontProbe* FontProbe::s_instance = nullptr;

FontProbe* FontProbe::instance() {
    if (!s_instance) s_instance = new FontProbe(nullptr);
    return s_instance;
}

FontProbe* FontProbe::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

FontProbe::FontProbe(QObject* parent)
    : QObject(parent) {
}

bool FontProbe::isAvailable(const QString& family) const {
    if (family.isEmpty()) return false;
    return QFontDatabase::hasFamily(family);
}

QString FontProbe::systemDefaultSans() const {
#ifdef Q_OS_WIN
    return QStringLiteral("Segoe UI");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("SF Pro Text");
#else
    return QStringLiteral("Sans Serif");
#endif
}

QString FontProbe::systemDefaultMono() const {
#ifdef Q_OS_WIN
    return QStringLiteral("Consolas");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("Menlo");
#else
    return QStringLiteral("Monospace");
#endif
}

QStringList FontProbe::availableFamilies() const {
    return QFontDatabase::families();
}

bool FontProbe::isFixedPitch(const QString& family) const {
    if (family.isEmpty()) return false;
    return QFontDatabase::isFixedPitch(family);
}

QStringList FontProbe::fixedPitchFamilies() const {
    if (!m_fixedPitchCache.isEmpty()) return m_fixedPitchCache;
    QStringList families;
    for (const QString& family : QFontDatabase::families()) {
        if (QFontDatabase::isFixedPitch(family)) families.append(family);
    }
    families.sort(Qt::CaseInsensitive);
    m_fixedPitchCache = families;
    return m_fixedPitchCache;
}

} // namespace NexusAI
