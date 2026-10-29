#pragma once

#include <QObject>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QQmlEngine>

#include "core/utils/SettingsDefinition.h"

namespace NexusAI {

class PreferenceRegistry : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(PreferenceRegistry)
public:
    explicit PreferenceRegistry(QObject* parent);

    static PreferenceRegistry* instance();
    static PreferenceRegistry* create(QQmlEngine*, QJSEngine*);

    bool setDefaultValue(const QString& key, const QVariant& value);
    QVariant normalizeValue(const QString& key, const QVariant& value) const;
    QVariant coerceStoredValue(const QString& key, const QVariant& value) const;
    bool validateValue(const QString& key, const QVariant& value, QString& error) const;

    Q_INVOKABLE bool hasPreference(const QString& key) const;
    Q_INVOKABLE QVariant defaultValue(const QString& key) const;
    Q_INVOKABLE QString displayName(const QString& key) const;
    // QML-facing metadata for one preference: {type: "Bool"|"Enum"|"Int"|"String",
    // displayName, defaultValue, options, constraint: {min, max, hasRange, ...}}.
    // Empty map for unknown keys.
    Q_INVOKABLE QVariantMap preferenceMeta(const QString& key) const;
    Q_INVOKABLE QStringList preferencesInCategory(const QString& categoryId) const;
    // 一级分组（导航用）：categoryGroups() 按 order 返回分组 id；
    // categoriesInGroup() 返回组内分类（按分类 order）；categoryGroup()
    // 返回某分类所属分组 id。分组只影响展示，不改变任何存储键。
    Q_INVOKABLE QStringList categoryGroups() const;
    Q_INVOKABLE QString categoryGroupName(const QString& groupId) const;
    Q_INVOKABLE QString categoryGroup(const QString& categoryId) const;
    Q_INVOKABLE QStringList categoriesInGroup(const QString& groupId) const;
    Q_INVOKABLE QStringList categories() const;
    Q_INVOKABLE QString categoryName(const QString& id) const;
    Q_INVOKABLE int categoryOrder(const QString& id) const;

    const PreferenceMeta* meta(const QString& key) const;
    const CategoryMeta* category(const QString& id) const;
    const CategoryGroupMeta* group(const QString& id) const;

private:
    void registerCategory(const QString& id, const QString& name, int order,
                          const QString& groupId);
    void registerGroup(const CategoryGroupMeta& meta);
    void registerPreference(const PreferenceMeta& meta);

    QHash<QString, PreferenceMeta> m_preferences;
    QHash<QString, CategoryMeta> m_categories;
    QHash<QString, CategoryGroupMeta> m_groups;

    static PreferenceRegistry* s_instance;
};

} // namespace NexusAI
