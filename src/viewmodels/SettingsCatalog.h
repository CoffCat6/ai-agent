#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QHash>
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
    // 值快照的失效计数：任何设置写入（偏好走 PreferenceStore::saved，
    // M1 键走 SettingsService::settingSaved）都会 +1。
    //
    // QML 里需要"值变了就重算"的绑定先读一次 revision 再调用下面的
    // Q_INVOKABLE——Q_INVOKABLE 本身没有变更通知，不建立依赖就只会求值一次。
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged FINAL)
public:
    explicit SettingsCatalog(QObject* parent = nullptr);

    static SettingsCatalog* instance();
    static SettingsCatalog* create(QQmlEngine*, QJSEngine*);

    int revision() const { return m_revision; }

    // 用户可见条目，按分类 order + 标题排序。
    Q_INVOKABLE QVariantList entries() const;
    // 大小写不敏感子串匹配：标题 + 关键词 + 键名/条目 id + 分类名。
    // 结果额外带 currentDisplay / modified / titleHtml（命中加粗）/
    // highlightStart / highlightLength / score（docs/31 O2）。
    Q_INVOKABLE QVariantList search(const QString& query) const;
    // 单条（包含隐藏条目）；未知 id 返回空 map。
    Q_INVOKABLE QVariantMap entry(const QString& id) const;
    // 覆盖性检查：登记过（含隐藏）即为 true。
    Q_INVOKABLE bool hasEntry(const QString& id) const;
    // "none" | "preference" | "page"——决定底栏是否显示「恢复默认」。
    Q_INVOKABLE QString resetMode(const QString& categoryId) const;

    // ── 值快照（docs/31 O1/O2）───────────────────────────
    // 一个条目的当前值/默认值/显示摘要/是否已修改/是否可复原。未知 id 返
    // 回带 known=false 的 map。密钥类条目只给"已配置/未配置"，不含任何值。
    Q_INVOKABLE QVariantMap describe(const QString& id) const;
    Q_INVOKABLE bool isModified(const QString& id) const;
    // 分类内"已修改"条目数，只统计可见且可复位的条目——导航徽标用它，
    // 与行级圆点保持同一口径。结果按 revision 缓存：导航每次写入都会让
    // 16 个分类各查一次，不缓存就是 16 遍全表 inspect。
    Q_INVOKABLE int modifiedCount(const QString& categoryId) const;
    // 把该条目复位为默认值：偏好走 EditBuffer.stage，M1 键走
    // SettingsService::setValueForSetting。密钥/凭据与无复位通道的条目
    // 一律拒绝（返回 false）。
    Q_INVOKABLE bool resetValue(const QString& id);

signals:
    void revisionChanged();

private:
    void bumpRevision();
    void rebuildModifiedCounts() const;

    int m_revision{0};
    // modifiedCount 的每 revision 缓存（mutable：查询不改可见状态）。
    mutable int m_modifiedCountRevision{-1};
    mutable QHash<QString, int> m_modifiedCounts;
};

} // namespace NexusAI
