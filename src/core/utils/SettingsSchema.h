#pragma once

#include "SettingsDefinition.h"

#include <QVector>

namespace NexusAI {

// Authoritative catalog for persisted settings and UI preferences. Preference
// registry metadata and backup validation are derived from these definitions.
class SettingsSchema final {
public:
    // v0 = pre-schema; v1 = structured schema; v2 = model lists became user
    // managed; v3/v4 normalize context-token defaults while retaining custom values.
    static constexpr int kSchemaVersion = 4;
    static constexpr int kDefaultLogMaxFileSizeMb = 10;
    static constexpr int kDefaultLogRetentionFiles = 14;

    static const QVector<CategoryMeta>& categories();
    static const QVector<PreferenceMeta>& preferences();
    static const QVector<SettingsKeyDef>& registeredKeys();
    static const SettingsKeyDef* definition(const QString& key);
    static QVariant defaultValue(const QString& key, const QVariant& fallback = {});
    static int clampInt(const QString& key, int value);
    static QStringList options(const QString& key);
    static bool isValidValue(const QString& key, const QVariant& value);

    static void migrate(int fromVersion);
};

} // namespace NexusAI
