#pragma once

#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace NexusAI {

// docs/31 N2：全局快捷键的唯一事实源。
//
// 动作表（id / 标题 / 默认序列）写在 C++ 里，main.qml 的 Shortcut 只绑定
// `bindings`，速查表与设置页只读 `actions`——序列不再在 QML 里抄第二份。
// 用户改动以 JSON 对象（actionId → PortableText 序列）存在声明式偏好
// `ui.shortcuts` 中，空对象即全部默认。
//
// 冲突与安全性在 assign() 一处拦截：全局动作必须带 Ctrl/Alt/Meta 或使用
// 功能键，且不得与其他动作当前生效的序列重复。外部写入（设置页底部
// “恢复本页默认值”、全局复位、EditBuffer）通过 PreferenceStore::saved
// 回到 reload()，界面与 main.qml 随之刷新。
class ShortcutMap : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(ShortcutMap)
    // 动作 id → 当前生效序列。main.qml 用 bindings[...] 绑定 Shortcut。
    Q_PROPERTY(QVariantMap bindings READ bindings NOTIFY changed)
    // 设置页 / 速查表用的完整动作列表（含标题、默认值、是否已修改）。
    Q_PROPERTY(QVariantList actions READ actions NOTIFY changed)
    Q_PROPERTY(int modifiedCount READ modifiedCount NOTIFY changed)
    // 录制期间为 true：main.qml 的全局 Shortcut 必须让位，否则按 Ctrl+]
    // 会先触发“切换面板”而录不到这个组合键。
    Q_PROPERTY(bool recording READ recording WRITE setRecording NOTIFY recordingChanged)
public:
    explicit ShortcutMap(QObject* parent);

    static ShortcutMap* instance();
    static ShortcutMap* create(QQmlEngine*, QJSEngine*);

    QVariantMap bindings() const;
    QVariantList actions() const;
    int modifiedCount() const;

    bool recording() const;
    void setRecording(bool recording);

    // 返回 {ok, error, conflict, sequence}；失败时值不变、不落盘。
    Q_INVOKABLE QVariantMap assign(const QString& actionId, const QString& sequence);
    Q_INVOKABLE bool resetAction(const QString& actionId);
    Q_INVOKABLE bool resetAll();
    // QML 录制的最后一步：KeyEvent.key / KeyEvent.modifiers → PortableText。
    // 纯修饰键或无法识别的键返回空串。
    Q_INVOKABLE QString sequenceFromKey(int key, int modifiers) const;

signals:
    void changed();
    void recordingChanged();

private:
    struct Action {
        QString id;
        QString title;
        QString description;
        QString defaultSequence;
    };

    static const QVector<Action>& actionTable();
    static const Action* findAction(const QString& id);
    static QString normalizeSequence(const QString& sequence);
    static QString keyIdentifier(int key, int modifiers);

    void reload();
    bool writeOverrides(const QHash<QString, QString>& overrides);

    QHash<QString, QString> m_overrides;   // 只存与默认值不同的项
    QHash<QString, QString> m_effective;   // 动作表全集 → 当前生效序列
    bool m_recording{false};

    static ShortcutMap* s_instance;
};

} // namespace NexusAI
