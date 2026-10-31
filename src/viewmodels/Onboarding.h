#pragma once

#include <QObject>
#include <QQmlEngine>

namespace NexusAI {

// docs/31 N1：首次启动向导的触发条件与「已看过」标记。
//
// 向导的交互在 QML（语言/主题 → 模型配置 → 可选导入），但「要不要弹」
// 必须单点判断：它同时依赖模型配置表与偏好键 `general.onboarded`，写进
// main.qml 的绑定里既难单测，也容易在设置恢复/导入后误触发。这里只做
// 判定与落盘，不碰任何 UI 状态。
class Onboarding : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(Onboarding)

public:
    // No default constructor on purpose (see AgentViewModel: the engine
    // would prefer Constructor over the create() factory).
    explicit Onboarding(QObject* parent);

    static Onboarding* instance();
    static Onboarding* create(QQmlEngine*, QJSEngine*);

    // 无任何模型配置且 `general.onboarded` 未写 → 全新用户。
    // 已有 profile 的用户即便标记丢失也不再打扰（配置本身就是"已完成"）。
    Q_INVOKABLE bool shouldShow() const;

    // 「完成」与「跳过」共用：记下已看过，返回是否写盘成功。重复调用
    // 不再写盘（否则每次启动都会多一条 saved 信号）。
    Q_INVOKABLE bool complete();
    Q_INVOKABLE bool isComplete() const;

private:
    static Onboarding* s_instance;
};

} // namespace NexusAI
