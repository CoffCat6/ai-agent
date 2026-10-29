#include "PreferenceStore.h"
#include "ChangeBus.h"
#include "core/utils/SettingsRepository.h"

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

PreferenceStore::PreferenceStore(QObject* parent) : QObject(parent) {
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
    if (SettingsRepository::contains(primary))
        return SettingsRepository::value(primary);

    for (const QString& legacyKey : m->legacyKeys) {
        if (SettingsRepository::contains(legacyKey))
            return convertLegacyValue(key, SettingsRepository::value(legacyKey));
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

bool PreferenceStore::hasStoredValue(const QString& key) const {
    const auto* reg = PreferenceRegistry::instance();
    return reg && reg->hasPreference(key)
        && SettingsRepository::contains(QStringLiteral("prefs/v1/") + key);
}

// Normalize persisted platform values and fall back when they no longer
// satisfy the registered type or constraints.
QVariant PreferenceStore::coerce(const QString& key, const QVariant& value) const {
    const auto* reg = PreferenceRegistry::instance();
    return reg ? reg->coerceStoredValue(key, value) : value;
}

bool PreferenceStore::save(const QString& key, const QVariant& value) {
    return saveBatch({{key, value}});
}

bool PreferenceStore::saveBatch(const QVariantMap& entries) {
    const auto* reg = PreferenceRegistry::instance();
    QVariantMap physicalEntries;
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
        if (!reg || !reg->hasPreference(it.key())) {
            qCWarning(lcPrefStore) << "saveBatch: unknown key" << it.key();
            emit persistenceError(tr("无法保存未登记的设置项：%1").arg(it.key()));
            return false;
        }
        const QVariant normalized = reg->normalizeValue(it.key(), it.value());
        QString validationError;
        if (!reg->validateValue(it.key(), normalized, validationError)) {
            emit persistenceError(validationError);
            return false;
        }
        physicalEntries.insert(QStringLiteral("prefs/v1/") + it.key(), normalized);
    }

    QString error;
    if (!SettingsRepository::writeBatch(physicalEntries, &error)) {
        qCWarning(lcPrefStore) << "saveBatch: persistence failed:" << error;
        emit persistenceError(tr("设置未能保存。请检查存储权限或磁盘空间后重试。"));
        return false;
    }

    if (m_changeBus) {
        for (auto it = physicalEntries.constBegin(); it != physicalEntries.constEnd(); ++it)
            m_changeBus->notify(it.key().mid(QStringLiteral("prefs/v1/").size()), it.value());
    }
    emit saved(entries.keys());
    return true;
}

bool PreferenceStore::reset(const QString& key) {
    return resetBatch({key});
}

bool PreferenceStore::resetBatch(const QStringList& keys) {
    const auto* reg = PreferenceRegistry::instance();
    QStringList physicalKeys;
    for (const QString& key : keys) {
        if (!reg || !reg->hasPreference(key)) {
            qCWarning(lcPrefStore) << "resetBatch: unknown key" << key;
            emit persistenceError(tr("无法重置未登记的设置项：%1").arg(key));
            return false;
        }
        physicalKeys.append(QStringLiteral("prefs/v1/") + key);
    }

    QString error;
    if (!SettingsRepository::removeBatch(physicalKeys, &error)) {
        qCWarning(lcPrefStore) << "resetBatch: persistence failed:" << error;
        emit persistenceError(tr("设置未能重置。请检查存储权限或磁盘空间后重试。"));
        return false;
    }

    if (m_changeBus) {
        for (const QString& key : keys)
            m_changeBus->notify(key, reg->defaultValue(key));
    }
    return true;
}

} // namespace NexusAI
