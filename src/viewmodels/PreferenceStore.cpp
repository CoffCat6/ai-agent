#include "PreferenceStore.h"
#include "ChangeBus.h"

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcPrefStore, "nexus.preferences.store")

namespace NexusAI {

PreferenceStore* PreferenceStore::s_instance = nullptr;

PreferenceStore* PreferenceStore::instance() {
    if (!s_instance) s_instance = new PreferenceStore(nullptr);
    return s_instance;
}

PreferenceStore* PreferenceStore::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

PreferenceStore::PreferenceStore(QObject* parent)
    : QObject(parent), m_settings("NexusAI", "NexusAI") {
}

void PreferenceStore::setChangeBus(ChangeBus* bus) {
    m_changeBus = bus;
}

QVariant PreferenceStore::loadWithLegacy(const QString& key) const {
    const auto* reg = PreferenceRegistry::instance();
    if (!reg) return {};

    const auto* m = reg->meta(key);
    if (!m) return {};

    const QString primary = QStringLiteral("prefs/v1/") + key;
    if (m_settings.contains(primary))
        return m_settings.value(primary);

    for (const QString& legacyKey : m->legacyKeys) {
        if (m_settings.contains(legacyKey))
            return convertLegacyValue(key, m_settings.value(legacyKey));
    }

    return m->defaultValue;
}

QVariant PreferenceStore::convertLegacyValue(const QString& key, const QVariant& value) const {
    if (key == QStringLiteral("ui.themeMode")) {
        const bool isDark = value.toBool();
        return isDark ? QStringLiteral("dark") : QStringLiteral("light");
    }
    return value;
}

QVariant PreferenceStore::load(const QString& key) const {
    return coerce(key, loadWithLegacy(key));
}

// QSettings' native Windows format round-trips bools as REG_SZ "true"/"false"
// strings and ints as REG_DWORD - normalize every load to the type declared
// in the registry metadata, or QML strict comparisons (=== true) and the
// EditBuffer's no-op staging detection silently break.
QVariant PreferenceStore::coerce(const QString& key, const QVariant& value) const {
    const auto* reg = PreferenceRegistry::instance();
    if (!reg) return value;
    const auto* m = reg->meta(key);
    if (!m) return value;

    switch (m->type) {
    case PreferenceType::Bool: {
        if (value.typeId() == QMetaType::Bool) return value;
        const QString s = value.toString();
        return QVariant(s == QLatin1String("true") || s == QLatin1String("1"));
    }
    case PreferenceType::Int: {
        bool ok = false;
        const int v = value.toInt(&ok);
        return ok ? QVariant(v) : m->defaultValue;
    }
    case PreferenceType::Enum:      // fall through
    case PreferenceType::String:
        return value.toString();
    }
    return value;
}

bool PreferenceStore::save(const QString& key, const QVariant& value) {
    const auto* reg = PreferenceRegistry::instance();
    if (!reg || !reg->hasPreference(key)) {
        qCWarning(lcPrefStore) << "save: unknown key" << key;
        return false;
    }

    const QString sk = QStringLiteral("prefs/v1/") + key;
    m_settings.setValue(sk, value);
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError) {
        qCWarning(lcPrefStore) << "save: sync failed for key" << key;
        return false;
    }

    if (m_changeBus)
        m_changeBus->notify(key, value);

    return true;
}

bool PreferenceStore::saveBatch(const QVariantMap& entries) {
    const auto* reg = PreferenceRegistry::instance();
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        if (!reg || !reg->hasPreference(it.key())) {
            qCWarning(lcPrefStore) << "saveBatch: unknown key" << it.key();
            continue;
        }
        const QString sk = QStringLiteral("prefs/v1/") + it.key();
        m_settings.setValue(sk, it.value());
    }
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError) {
        qCWarning(lcPrefStore) << "saveBatch: sync failed";
        return false;
    }

    if (m_changeBus) {
        for (auto it = entries.constBegin(); it != entries.constEnd(); ++it)
            m_changeBus->notify(it.key(), it.value());
    }
    return true;
}

bool PreferenceStore::reset(const QString& key) {
    const auto* reg = PreferenceRegistry::instance();
    if (!reg || !reg->hasPreference(key)) return false;

    const QString sk = QStringLiteral("prefs/v1/") + key;
    m_settings.remove(sk);
    m_settings.sync();

    const QVariant def = reg->defaultValue(key);
    if (m_changeBus)
        m_changeBus->notify(key, def);
    return true;
}

bool PreferenceStore::resetBatch(const QStringList& keys) {
    bool allOk = true;
    for (const QString& key : keys) {
        if (!reset(key)) allOk = false;
    }
    return allOk;
}

} // namespace NexusAI
