#pragma once

#include <QString>
#include <QVariant>

namespace NexusAI {

// 一个设置键的只读快照：当前值、默认值、是否已修改、是否可显示。
//
// 设置中心有三处 UI 需要「当前值」——行级"已修改"标识、导航徽标、
// 搜索结果的当前值摘要。它们必须共用同一份比较与格式化逻辑，否则同一个
// 键在三处会给出三种说法（docs/31 O1/O2）。
struct SettingValueSnapshot {
    bool known{false};      // 目录里有这条（含仅登记未显示的键）
    bool secret{false};     // 值不可显示（加密密钥 / 代理凭据）
    bool configured{false}; // 密钥/凭据是否已存在
    bool stored{false};     // 是否落过盘（没落盘 = 仍是默认，不算修改）
    bool modified{false};   // 当前值 != 默认值
    QVariant current;
    QVariant defaultValue;
};

namespace SettingsValue {

// 读取一个键的当前值快照。
//
// 纪律（docs/31 §6 红线 1）：
//   * 加密键只回答"已配置/未配置"，绝不返回任何值——即使 SettingsRepository
//     拿到的是 dpapi 密文，也没有任何理由让它离开这里；
//   * 代理凭据整体存在加密的 network/proxyAuth blob 里，目录里的
//     network/proxyUsername / network/proxyPassword 只是锚点、没有独立存储，
//     因此同样按凭据处理（不显示、不参与修改比较、不参与复位）。
SettingValueSnapshot inspect(const QString& key);

// 本地化的单行摘要：Bool→开/关，Enum→标签，String→截断，密钥→已配置/未配置。
QString displayOf(const SettingValueSnapshot& snapshot, const QString& key);

// 用与设置中心完全相同的规则渲染"某个键在某个值下会显示成什么"。
// 导入预览的逐键差异需要同时展示"当前值"和"备份里的值"，后者不在
// 存储里，不能走 inspect() 拿到现成快照。
QString displayValue(const QString& key, const QVariant& value);

// 与设置中心相同的值比较口径（Bool/Int/String/StringList/Other）。
// 导入 diff 用它判断"这个键会不会被改变"。
bool sameValueForKey(const QString& key, const QVariant& value,
                     const QVariant& expected);

} // namespace SettingsValue
} // namespace NexusAI
