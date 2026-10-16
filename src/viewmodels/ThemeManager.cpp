#include "ThemeManager.h"
#include "PreferenceRegistry.h"
#include "PreferenceStore.h"
#include "ChangeBus.h"
#include "FontProbe.h"

#include "core/utils/SettingsSchema.h"

#include <QGuiApplication>
#include <QStyleHints>

namespace NexusAI {
namespace {

QString automaticThemeMode() {
    return SettingsSchema::defaultValue(QStringLiteral("prefs/v1/ui.themeMode")).toString();
}

} // namespace

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
    : QObject(parent) {
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

    // These defaults depend on installed fonts; all other preference metadata
    // is loaded centrally from SettingsSchema.
    auto* probe = FontProbe::instance();
    const QString sansDefault = probe->isAvailable(QStringLiteral("Inter"))
        ? QStringLiteral("Inter") : probe->systemDefaultSans();
    const QString monoDefault = probe->isAvailable(QStringLiteral("JetBrains Mono"))
        ? QStringLiteral("JetBrains Mono") : probe->systemDefaultMono();
    reg->setDefaultValue(QStringLiteral("ui.fontSans"), sansDefault);
    reg->setDefaultValue(QStringLiteral("ui.fontMono"), monoDefault);
}

void ThemeManager::loadTheme() {
    const QVariant val = PreferenceStore::instance()->load(QStringLiteral("ui.themeMode"));
    m_themeMode = val.toString();
    // Defensive: a corrupt/unknown token must not leave the UI in a mode the
    // settings combo cannot name - fall back to the auto default.
    if (!SettingsSchema::options(QStringLiteral("prefs/v1/ui.themeMode"))
             .contains(m_themeMode))
        m_themeMode = SettingsSchema::defaultValue(
            QStringLiteral("prefs/v1/ui.themeMode")).toString();
    m_isDark = (m_themeMode == automaticThemeMode())
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
            const bool dark = (mode == automaticThemeMode())
                ? resolveSystemDark()
                : (mode == QStringLiteral("dark"));
            // docs/25 E6：不再回写 legacy 键 theme/dark——该键写而不读
            //（仅启动迁移读一次），唯一事实源是 prefs/v1/ui.themeMode。
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
    const QStringList allowedModes = SettingsSchema::options(
        QStringLiteral("prefs/v1/ui.themeMode"));
    const QString normalized = allowedModes.contains(mode)
        ? mode
        : automaticThemeMode();
    if (m_themeMode == normalized) return;
    m_themeMode = normalized;
    PreferenceStore::instance()->save(QStringLiteral("ui.themeMode"), normalized);
    const bool dark = (normalized == automaticThemeMode())
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
    if (m_themeMode != automaticThemeMode()) return;
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
    size = SettingsSchema::clampInt(QStringLiteral("prefs/v1/ui.baseFontSize"), size);
    if (m_baseFontSize == size) return;
    m_baseFontSize = size;
    PreferenceStore::instance()->save(QStringLiteral("ui.baseFontSize"), size);
    emit fontChanged();
}

} // namespace NexusAI
