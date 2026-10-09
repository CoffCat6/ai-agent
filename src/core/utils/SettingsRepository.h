#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace NexusAI {

// Shared persistence boundary for application settings. Keep this identity
// stable: changing it would make existing user settings appear to disappear.
class SettingsRepository final {
public:
    static QVariant value(const QString& key, const QVariant& defaultValue = {});
    static bool contains(const QString& key);
    static bool readBatch(const QStringList& keys, QVariantMap* values,
                          QStringList* presentKeys = nullptr, QString* error = nullptr);

    static bool writeValue(const QString& key, const QVariant& value,
                           QString* error = nullptr);
    static bool writeBatch(const QVariantMap& values, QString* error = nullptr);
    static bool removeBatch(const QStringList& keys, QString* error = nullptr);
};

} // namespace NexusAI
