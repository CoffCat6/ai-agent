#include "I18nManager.h"
#include "PreferenceStore.h"
#include "ChangeBus.h"
#include "LocaleProbe.h"

#include <QGuiApplication>
#include <QLocale>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcI18n, "nexus.i18n")

namespace NexusAI {

I18nManager* I18nManager::s_instance = nullptr;

I18nManager* I18nManager::instance() {
    if (!s_instance) s_instance = new I18nManager(nullptr);
    return s_instance;
}

I18nManager* I18nManager::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

I18nManager::I18nManager(QObject* parent)
    : QObject(parent) {
    m_currentLocale = detectInitialLocale();
    subscribeChanges();
}


QString I18nManager::detectInitialLocale() const {
    auto* store = PreferenceStore::instance();
    if (store) {
        const QVariant val = store->load(QStringLiteral("ui.locale"));
        const QString stored = val.toString();
        if (val.isValid() && !stored.isEmpty() && stored != QLatin1String("auto"))
            return stored;
    }

    const QLocale sys = QLocale::system();
    if (sys.language() == QLocale::Chinese)
        return QStringLiteral("zh-CN");
    return QStringLiteral("en");
}

void I18nManager::subscribeChanges() {
    auto* bus = ChangeBus::instance();
    if (!bus) return;
    connect(bus, &ChangeBus::changed, this, [this](const QString& key, const QVariant& value) {
        if (key == QStringLiteral("ui.locale"))
            applyLocale(value.toString());
    });
}

QStringList I18nManager::availableLocales() const {
    return LocaleProbe::instance()->availableLocales();
}

void I18nManager::applyLocale(const QString& locale) {
    QString target = locale;
    if (target.isEmpty() || target == QLatin1String("auto"))
        target = QLocale::system().language() == QLocale::Chinese
            ? QStringLiteral("zh-CN") : QStringLiteral("en");

    if (m_translatorInstalled) {
        QGuiApplication::removeTranslator(&m_translator);
        m_translatorInstalled = false;
    }

    // The UI source strings are Chinese, so every locale goes through a
    // translator: zh-CN uses the identity .ts, en the translated .ts.
    // Must use the QLocale overload: the QString overload treats its first
    // argument as the FILE name, so passing the locale there silently
    // resolves to load("zh_CN" as file, dir="nexus_ai", ...) - always
    // failing with "Translation resource missing".
    if (!m_translator.load(QLocale(target), QStringLiteral("nexus_ai"),
                           QStringLiteral("_"), QStringLiteral(":/i18n"))) {
        // Source text (Chinese) stays visible; do not install a partial or
        // stale translator.
        qCWarning(lcI18n) << "Translation resource missing for" << target
                          << "- falling back to source text";
    } else {
        QGuiApplication::installTranslator(&m_translator);
        m_translatorInstalled = true;
        qCInfo(lcI18n) << "Loaded translation:" << target;
    }

    if (m_currentLocale == target) return;
    m_currentLocale = target;
    emit localeChanged(target);
}

} // namespace NexusAI
