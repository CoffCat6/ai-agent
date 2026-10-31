#include "AppInfo.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>

namespace NexusAI {

AppInfo* AppInfo::s_instance = nullptr;

AppInfo* AppInfo::instance() {
    if (!s_instance) s_instance = new AppInfo(nullptr);
    return s_instance;
}

AppInfo* AppInfo::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

AppInfo::AppInfo(QObject* parent)
    : QObject(parent) {
}

QString AppInfo::appVersion() const {
    return QCoreApplication::applicationVersion();
}

QString AppInfo::qtVersion() const {
    return QStringLiteral(QT_VERSION_STR);
}

QString AppInfo::compiler() const {
#if defined(__GNUC__)
    return QStringLiteral("GCC %1.%2.%3")
        .arg(__GNUC__).arg(__GNUC_MINOR__).arg(__GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
    return QStringLiteral("MSVC %1").arg(_MSC_VER);
#else
    return QStringLiteral("unknown");
#endif
}

QString AppInfo::architecture() const {
    return QSysInfo::currentCpuArchitecture();
}

QString AppInfo::osVersion() const {
    return QSysInfo::prettyProductName();
}

QString AppInfo::dataDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QStringList AppInfo::licenses() const {
    return {
        QStringLiteral("Qt 6 — LGPL-3.0 / GPL-3.0"),
        QStringLiteral("SQLite — Public Domain"),
    };
}

bool AppInfo::openDataDir() const {
    const QString dir = dataDir();
    if (dir.isEmpty()) return false;
    if (!QDir().mkpath(dir)) return false;
    return QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

} // namespace NexusAI
