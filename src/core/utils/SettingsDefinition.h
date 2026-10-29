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

// 设置中心字段级目录（SettingsCatalog）的一条记录。
//
// 目的：搜索与定位不再依赖页面自己维护的字符串索引。声明式偏好由
// PreferenceRegistry 自动派生；SettingsService 的 M1 键与页面级操作入口在
// SettingsCatalog 的静态表里显式登记（id == QML 锚点 objectName）。
enum class CatalogResetScope {
    None,       // 不参与「恢复默认」
    Preference, // 由 EditBuffer.resetToDefault 复位
    Page        // 由页面的 requestResetDefaults() 复位
};

struct CatalogEntryDef {
    QString id;          // 稳定标识，同时是目标的 objectName
    QString categoryId;
    QString title;       // QT_TRANSLATE_NOOP("SettingsSearch", ...) 源串
    QString keywords;
    bool hidden{false};  // 登记但不出现在搜索结果里（内部键/被合成项覆盖）
    CatalogResetScope reset{CatalogResetScope::None};
};


} // namespace NexusAI
