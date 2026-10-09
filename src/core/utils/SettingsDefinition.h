#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>

namespace NexusAI {

enum class PreferenceType {
    Bool,
    Enum,
    Int,
    String
};

struct PreferenceConstraint {
    int min{0};
    int max{0};
    QStringList options;
    int maxLength{0};
    bool required{false};
    bool hasRange{false};
};

struct PreferenceMeta {
    QString key;
    QString categoryId;
    QString displayName;
    QVariant defaultValue;
    PreferenceType type{PreferenceType::String};
    PreferenceConstraint constraint;
    QStringList legacyKeys;
};

struct CategoryMeta {
    QString id;
    QString name;
    int order{0};
};

enum class SettingValueType {
    Any,
    Bool,
    Int,
    Double,
    String,
    StringList
};

struct SettingsKeyDef {
    QString key;
    QVariant defaultValue;
    SettingValueType type{SettingValueType::Any};
    PreferenceConstraint constraint;
    bool encrypted{false};
    bool legacy{false};
};

} // namespace NexusAI
