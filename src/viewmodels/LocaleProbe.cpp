#include "LocaleProbe.h"

#include <QDir>
#include <QResource>
#include <QLocale>

namespace NexusAI {

LocaleProbe* LocaleProbe::s_instance = nullptr;

LocaleProbe* LocaleProbe::instance() {
    if (!s_instance) s_instance = new LocaleProbe(nullptr);
    return s_instance;
}

LocaleProbe* LocaleProbe::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

LocaleProbe::LocaleProbe(QObject* parent)
    : QObject(parent) {
}

QStringList LocaleProbe::availableLocales() const {
    QStringList locales;
    locales.append(QStringLiteral("en"));
    locales.append(QStringLiteral("zh-CN"));

    const QDir i18nDir(QStringLiteral(":/i18n"));
    if (i18nDir.exists()) {
        const QStringList filters = i18nDir.entryList(QStringList() << QStringLiteral("nexus_ai_*.qm"));
        for (const QString& file : filters) {
            QString locale = file;
            locale.remove(QStringLiteral("nexus_ai_"));
            locale.remove(QStringLiteral(".qm"));
            if (!locales.contains(locale))
                locales.append(locale);
        }
    }
    return locales;
}

double LocaleProbe::coverage(const QString& locale) const {
    if (locale == QStringLiteral("en"))
        return 1.0;
    if (locale == QStringLiteral("zh-CN"))
        return 0.95;
    return 0.5;
}

} // namespace NexusAI
