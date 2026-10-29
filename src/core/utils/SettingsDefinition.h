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

// 设置中心导航的一级分组（模型与对话 / 外观与常规 / 知识与内容 / 数据与诊断）。
// CategoryMeta::groupId 指向这里；group 只影响展示层级，不影响任何存储键。
struct CategoryGroupMeta {
    QString id;
    QString name;
    int order{0};
};

struct CategoryMeta {
    QString id;
    QString name;
    // 全局排序：search 结果仍按它排序；同一分组的分类在列表中连续。
    int order{0};
    QString groupId;
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
