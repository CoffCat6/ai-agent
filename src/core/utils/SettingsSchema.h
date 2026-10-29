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

    static const QVector<CategoryGroupMeta>& categoryGroups();
    static const QVector<CategoryMeta>& categories();
    static const QVector<PreferenceMeta>& preferences();
    static const QVector<SettingsKeyDef>& registeredKeys();
    static const SettingsKeyDef* definition(const QString& key);
    static QVariant defaultValue(const QString& key, const QVariant& fallback = {});
    static int clampInt(const QString& key, int value);
    static QStringList options(const QString& key);
    static bool isValidValue(const QString& key, const QVariant& value);

    // docs/25 E8：int/bool 设置键读取的统一入口（存储值 → schema 默认值 →
    // clamp 一站式）。配额键曾由 AIAgent::checkQuota 与 SettingsService::load
    // 各写一份三段式样板形成双源，收敛于此；新代码读设置键时优先使用。
    static int intSetting(const QString& key);
    static bool boolSetting(const QString& key);
};

} // namespace NexusAI
