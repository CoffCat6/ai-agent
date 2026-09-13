#include "ThemeManager.h"
#include "PreferenceRegistry.h"
#include "PreferenceStore.h"
#include "ChangeBus.h"
#include "FontProbe.h"

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
    m_changeBus = ChangeBus::instance();

    registerPreferences();
    loadTheme();
    loadFontPrefs();
    subscribeChanges();
}

void ThemeManager::registerPreferences() {
    auto* reg = PreferenceRegistry::instance();
    if (!reg) return;
    if (reg->hasPreference(QStringLiteral("ui.themeMode"))) return;

    // Font defaults fall back to the platform family when the brand font is
    // not installed - a missing default would render via silent fallback
    // while the settings combo shows a blank selection.
    auto* probe = FontProbe::instance();
    const QString sansDefault = probe->isAvailable(QStringLiteral("Inter"))
        ? QStringLiteral("Inter") : probe->systemDefaultSans();
    const QString monoDefault = probe->isAvailable(QStringLiteral("JetBrains Mono"))
        ? QStringLiteral("JetBrains Mono") : probe->systemDefaultMono();

    {
        PreferenceMeta meta;
        meta.key = QStringLiteral("ui.themeMode");
        meta.categoryId = QStringLiteral("interface");
        meta.displayName = QT_TRANSLATE_NOOP("NexusAI", "主题模式");
        meta.defaultValue = QStringLiteral("dark");
        meta.type = PreferenceType::Enum;
        meta.constraint.options = { QStringLiteral("dark"), QStringLiteral("light") };
        meta.legacyKeys = { QStringLiteral("theme/dark") };
        reg->registerPreference(meta);
    }
    {
        PreferenceMeta meta;
        meta.key = QStringLiteral("ui.fontSans");
        meta.categoryId = QStringLiteral("interface");
        meta.displayName = QT_TRANSLATE_NOOP("NexusAI", "正文字体");
        meta.defaultValue = sansDefault;
        meta.type = PreferenceType::String;
        reg->registerPreference(meta);
    }
    {
        PreferenceMeta meta;
        meta.key = QStringLiteral("ui.fontMono");
        meta.categoryId = QStringLiteral("interface");
        meta.displayName = QT_TRANSLATE_NOOP("NexusAI", "等宽字体");
        meta.defaultValue = monoDefault;
        meta.type = PreferenceType::String;
        reg->registerPreference(meta);
    }
    {
        PreferenceMeta meta;
        meta.key = QStringLiteral("ui.baseFontSize");
        meta.categoryId = QStringLiteral("interface");
        meta.displayName = QT_TRANSLATE_NOOP("NexusAI", "基础字号");
        meta.defaultValue = 14;
        meta.type = PreferenceType::Int;
        meta.constraint.min = 10;
        meta.constraint.max = 24;
        meta.constraint.hasRange = true;
        reg->registerPreference(meta);
    }
}

void ThemeManager::loadTheme() {
    const QVariant val = PreferenceStore::instance()->load(QStringLiteral("ui.themeMode"));
    if (val.isValid()) {
        m_isDark = (val.toString() != QStringLiteral("light"));
    } else {
        m_isDark = m_settings.value("theme/dark", true).toBool();
    }
}

void ThemeManager::loadFontPrefs() {
    auto* store = PreferenceStore::instance();
    auto* probe = FontProbe::instance();

    const QVariant sans = store->load(QStringLiteral("ui.fontSans"));
    if (sans.isValid() && probe->isAvailable(sans.toString()))
        m_fontSans = sans.toString();
    const QVariant mono = store->load(QStringLiteral("ui.fontMono"));
    if (mono.isValid() && probe->isAvailable(mono.toString()))
        m_fontMono = mono.toString();
    const QVariant size = store->load(QStringLiteral("ui.baseFontSize"));
    if (size.isValid()) m_baseFontSize = size.toInt();

    // Stored fonts may have been uninstalled since; render with the platform
    // default instead of a silent fallback the settings UI cannot name.
    if (!probe->isAvailable(m_fontSans))
        m_fontSans = probe->systemDefaultSans();
    if (!probe->isAvailable(m_fontMono))
        m_fontMono = probe->systemDefaultMono();
}

void ThemeManager::subscribeChanges() {
    if (!m_changeBus) return;
    connect(m_changeBus, &ChangeBus::changed, this, [this](const QString& key, const QVariant& value) {
        // The setters below echo their own notify() back through the bus;
        // ignore those so a save neither rewrites the same value nor
        // re-emits the change signals.
        if (key == QStringLiteral("ui.themeMode")) {
            const bool dark = value.toString() != QStringLiteral("light");
            if (dark == m_isDark) return;
            m_isDark = dark;
            saveTheme(dark);
            emit themeChanged();
        } else if (key == QStringLiteral("ui.fontSans")) {
            const QString family = value.toString();
            if (family == m_fontSans) return;
            m_fontSans = family;
            emit fontChanged();
        } else if (key == QStringLiteral("ui.fontMono")) {
            const QString family = value.toString();
            if (family == m_fontMono) return;
            m_fontMono = family;
            emit fontChanged();
        } else if (key == QStringLiteral("ui.baseFontSize")) {
            const int size = value.toInt();
            if (size == m_baseFontSize) return;
            m_baseFontSize = size;
            emit fontChanged();
        }
    });
}

void ThemeManager::saveTheme(bool dark) {
    m_settings.setValue("theme/dark", dark);
    m_settings.sync();
}

void ThemeManager::setIsDark(bool dark) {
    if (m_isDark == dark) return;
    m_isDark = dark;
    // Persist through the preference store (single source of truth); the
    // bus echo lands back in subscribeChanges and is skipped there.
    PreferenceStore::instance()->save(QStringLiteral("ui.themeMode"),
                                      dark ? QStringLiteral("dark") : QStringLiteral("light"));
    emit themeChanged();
}

void ThemeManager::setFontSans(const QString& family) {
    if (m_fontSans == family) return;
    m_fontSans = family;
    PreferenceStore::instance()->save(QStringLiteral("ui.fontSans"), family);
    emit fontChanged();
}

void ThemeManager::setFontMono(const QString& family) {
    if (m_fontMono == family) return;
    m_fontMono = family;
    PreferenceStore::instance()->save(QStringLiteral("ui.fontMono"), family);
    emit fontChanged();
}

void ThemeManager::setBaseFontSize(int size) {
    size = std::max(10, std::min(24, size));
    if (m_baseFontSize == size) return;
    m_baseFontSize = size;
    PreferenceStore::instance()->save(QStringLiteral("ui.baseFontSize"), size);
    emit fontChanged();
}

} // namespace NexusAI
