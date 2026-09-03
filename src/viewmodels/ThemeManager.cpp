#include "ThemeManager.h"

namespace NexusAI {

ThemeManager* ThemeManager::s_instance = nullptr;

ThemeManager* ThemeManager::instance() {
    if (!s_instance) s_instance = new ThemeManager(nullptr);
    return s_instance;
}

ThemeManager* ThemeManager::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent), m_settings("NexusAI", "NexusAI") {
    loadTheme();
}

void ThemeManager::loadTheme() {
    m_isDark = m_settings.value("theme/dark", true).toBool();
}

void ThemeManager::saveTheme(bool dark) {
    m_settings.setValue("theme/dark", dark);
    m_settings.sync();
}

void ThemeManager::setIsDark(bool dark) {
    if (m_isDark == dark) return;
    m_isDark = dark;
    saveTheme(dark);
    emit themeChanged();
}

} // namespace NexusAI
