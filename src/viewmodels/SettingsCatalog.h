#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>
#include <QString>

namespace NexusAI {

// 设置中心的字段级目录：搜索、跳转定位与「恢复默认」可见性都从这里取，
// 不再依赖 settings-search.js 的手工标题索引。
//
// 条目来源有两条：
//   * 声明式偏好（SettingsSchema::preferences()）——分类、显示名、键名都
//     由 PreferenceRegistry 提供，零维护；
//   * SettingsService 的 M1 键与页面级操作入口——在 SettingsCatalog.cpp 的
//     静态表里登记。id 同时也是 QML 目标的 objectName，页面只需在对应
//     SettingRow/SettingsField/SettingsSection 上写 `objectName: "<id>"`。
//
// 单测（test_settings）断言每个 SettingsSchema::registeredKeys() 都有条目，
// 防止新增设置键后忘记登记（漏登记 = 搜索不到、也定位不到）。
class SettingsCatalog : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(SettingsCatalog)
public:
    explicit SettingsCatalog(QObject* parent = nullptr);

    static SettingsCatalog* instance();
    static SettingsCatalog* create(QQmlEngine*, QJSEngine*);

    // 用户可见条目，按分类 order + 标题排序。
    Q_INVOKABLE QVariantList entries() const;
    // 大小写不敏感子串匹配：标题 + 关键词 + 键名/条目 id + 分类名。
    Q_INVOKABLE QVariantList search(const QString& query) const;
    // 单条（包含隐藏条目）；未知 id 返回空 map。
    Q_INVOKABLE QVariantMap entry(const QString& id) const;
    // 覆盖性检查：登记过（含隐藏）即为 true。
    Q_INVOKABLE bool hasEntry(const QString& id) const;
    // "none" | "preference" | "page"——决定底栏是否显示「恢复默认」。
    Q_INVOKABLE QString resetMode(const QString& categoryId) const;
};

} // namespace NexusAI
