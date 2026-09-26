#include "ThemeManager.h"
#include "PreferenceRegistry.h"
#include "PreferenceStore.h"
#include "ChangeBus.h"
#include "FontProbe.h"

#include <QGuiApplication>
#include <QStyleHints>

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

    // REQ-T5-02: follow the OS light/dark scheme live while the preference
    // is "auto". Explicit dark/light choices ignore system changes.
    if (qApp) {
        if (auto* hints = QGuiApplication::styleHints()) {
            connect(hints, &QStyleHints::colorSchemeChanged, this,
                    [this](Qt::ColorScheme) { applySystemColorScheme(); });
        }
    }
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
        meta.defaultValue = QStringLiteral("auto");
        meta.type = PreferenceType::Enum;
        meta.constraint.options = { QStringLiteral("auto"), QStringLiteral("dark"), QStringLiteral("light") };
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
    m_themeMode = val.toString();
    // Defensive: a corrupt/unknown token must not leave the UI in a mode the
    // settings combo cannot name - fall back to the auto default.
    if (m_themeMode != QStringLiteral("auto") &&
        m_themeMode != QStringLiteral("dark") &&
        m_themeMode != QStringLiteral("light")) {
        m_themeMode = QStringLiteral("auto");
    }
    m_isDark = (m_themeMode == QStringLiteral("auto"))
        ? resolveSystemDark()
        : (m_themeMode == QStringLiteral("dark"));
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
            const QString mode = value.toString();
            if (mode == m_themeMode) return;
            m_themeMode = mode;
            const bool dark = (mode == QStringLiteral("auto"))
                ? resolveSystemDark()
                : (mode == QStringLiteral("dark"));
            saveTheme(dark);
            emit themeModeChanged();
            if (dark != m_isDark) {
                m_isDark = dark;
                emit themeChanged();
            }
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
    // REQ-T5-04: an explicit toggle (settings or Ctrl+D) always upgrades an
    // "auto" preference to a concrete mode so the user's choice survives a
    // later OS scheme change.
    const QString explicitMode = dark ? QStringLiteral("dark") : QStringLiteral("light");
    if (m_isDark == dark && m_themeMode == explicitMode) return;
    m_isDark = dark;
    const bool modeChanged = (m_themeMode != explicitMode);
    if (modeChanged) m_themeMode = explicitMode;
    // Persist through the preference store (single source of truth); the
    // bus echo lands back in subscribeChanges and is skipped there.
    PreferenceStore::instance()->save(QStringLiteral("ui.themeMode"), explicitMode);
    if (modeChanged) emit themeModeChanged();
    emit themeChanged();
}

void ThemeManager::setThemeMode(const QString& mode) {
    // REQ-T5-01/03: "auto" follows the system; "dark"/"light" are explicit.
    const QString normalized = (mode == QStringLiteral("dark") ||
                                mode == QStringLiteral("light"))
        ? mode
        : QStringLiteral("auto");
    if (m_themeMode == normalized) return;
    m_themeMode = normalized;
    PreferenceStore::instance()->save(QStringLiteral("ui.themeMode"), normalized);
    const bool dark = (normalized == QStringLiteral("auto"))
        ? resolveSystemDark()
        : (normalized == QStringLiteral("dark"));
    emit themeModeChanged();
    if (dark != m_isDark) {
        m_isDark = dark;
        emit themeChanged();
    }
}

bool ThemeManager::resolveSystemDark() const {
    if (!qApp) return true;   // no GUI app: keep the historical dark default
    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
    // Unknown (an OS that cannot report a scheme) keeps the dark default so
    // the look never flips without the user asking.
    return scheme != Qt::ColorScheme::Light;
}

void ThemeManager::applySystemColorScheme() {
    // REQ-T5-03: explicit dark/light choices are immune to system changes.
    if (m_themeMode != QStringLiteral("auto")) return;
    const bool dark = resolveSystemDark();
    if (dark == m_isDark) return;
    m_isDark = dark;
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
