#include "SettingsRepository.h"
#include "SettingsSchema.h"

#include <QHash>
#include <QSet>
#include <QSettings>

namespace NexusAI {
namespace {

constexpr auto kOrganization = "NexusAI";
constexpr auto kApplication = "NexusAI";
} // namespace

QVariant SettingsRepository::value(const QString& key, const QVariant& defaultValue) {
    const QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                             QString::fromLatin1(kOrganization),
                             QString::fromLatin1(kApplication));
    return settings.value(key, defaultValue);
}

bool SettingsRepository::contains(const QString& key) {
    const QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                             QString::fromLatin1(kOrganization),
                             QString::fromLatin1(kApplication));
    return settings.contains(key);
}

bool SettingsRepository::readBatch(const QStringList& keys, QVariantMap* values,
                                   QStringList* presentKeys, QString* error) {
    if (!values) {
        if (error) *error = QStringLiteral("读取设置时未提供结果容器");
        return false;
    }

    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                       QString::fromLatin1(kOrganization),
                       QString::fromLatin1(kApplication));
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        if (error) {
            *error = QStringLiteral("QSettings 同步失败（状态码 %1）")
                         .arg(static_cast<int>(settings.status()));
        }
        return false;
    }

    values->clear();
    if (presentKeys) presentKeys->clear();
    for (const QString& key : keys) {
        if (!settings.contains(key)) continue;
        values->insert(key, settings.value(key));
        if (presentKeys && !presentKeys->contains(key)) presentKeys->append(key);
    }
    return true;
}

bool SettingsRepository::writeValue(const QString& key, const QVariant& value,
                                    QString* error) {
    return writeBatch({{key, value}}, error);
}

bool SettingsRepository::writeBatch(const QVariantMap& values, QString* error) {
    if (values.isEmpty()) return true;

    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (SettingsSchema::definition(it.key())
                && !SettingsSchema::isValidValue(it.key(), it.value())) {
            if (error)
                *error = QStringLiteral("设置值类型或范围无效：%1").arg(it.key());
            return false;
        }
    }

    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                       QString::fromLatin1(kOrganization),
                       QString::fromLatin1(kApplication));
    QHash<QString, QVariant> previousValues;
    QSet<QString> existingKeys;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (settings.contains(it.key())) {
            existingKeys.insert(it.key());
            previousValues.insert(it.key(), settings.value(it.key()));
        }
    }

    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        settings.setValue(it.key(), it.value());

    settings.sync();
    if (settings.status() == QSettings::NoError) return true;

    const QSettings::Status writeStatus = settings.status();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        if (existingKeys.contains(it.key()))
            settings.setValue(it.key(), previousValues.value(it.key()));
        else
            settings.remove(it.key());
    }
    settings.sync(); // Best-effort rollback; QSettings writes are not atomic.
    if (error) {
        *error = QStringLiteral("QSettings 同步失败（状态码 %1）")
                     .arg(static_cast<int>(writeStatus));
    }
    return false;
}

bool SettingsRepository::removeBatch(const QStringList& keys, QString* error) {
    if (keys.isEmpty()) return true;

    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                       QString::fromLatin1(kOrganization),
                       QString::fromLatin1(kApplication));
    QHash<QString, QVariant> previousValues;
    QSet<QString> existingKeys;
    for (const QString& key : keys) {
        if (settings.contains(key)) {
            existingKeys.insert(key);
            previousValues.insert(key, settings.value(key));
        }
    }

    for (const QString& key : keys)
        settings.remove(key);

    settings.sync();
    if (settings.status() == QSettings::NoError) return true;

    const QSettings::Status writeStatus = settings.status();
    for (const QString& key : keys) {
        if (existingKeys.contains(key))
            settings.setValue(key, previousValues.value(key));
        else
            settings.remove(key);
    }
    settings.sync(); // Best-effort rollback; QSettings writes are not atomic.
    if (error) {
        *error = QStringLiteral("QSettings 同步失败（状态码 %1）")
                     .arg(static_cast<int>(writeStatus));
    }
    return false;
}

} // namespace NexusAI
