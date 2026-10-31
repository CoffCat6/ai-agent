#pragma once

#include <QtGlobal>

// docs/31 S4.2 N6：数据库自动备份的调度策略。
//
// 间隔只开放五档（关闭 / 1 小时 / 6 小时 / 1 天 / 7 天）。界面只提供这五档，
// 但设置导入、手工改注册表、旧版本配置都可能写入任意整数，所以所有入口统一
// 经 normalizeBackupIntervalHours() 归一后再启动定时器——否则会出现"每 37
// 分钟备份一次"这种用户无法理解、也无法从界面上改回来的频率。
//
// 归一规则：取**不超过**输入值的最大档位（备份频率不低于用户写入的意图）。
// 例：2 → 1、23 → 6、167 → 24、999 → 168。负数与 0 一律视为关闭。
// 这里刻意不依赖 QObject/QTimer：纯函数，测试可直接断言。
namespace NexusAI {

// 必须升序排列——归一依赖"向下取档"。
inline constexpr int kBackupIntervalHours[] = {0, 1, 6, 24, 168};
inline constexpr int kBackupIntervalOptionCount =
    sizeof(kBackupIntervalHours) / sizeof(kBackupIntervalHours[0]);
// 与 SettingsSchema 中 memory/backupIntervalHours 的 intRange 上限一致。
inline constexpr int kMaxBackupIntervalHours = 168;

inline int normalizeBackupIntervalHours(int hours) {
    if (hours <= 0) return 0;
    if (hours >= kMaxBackupIntervalHours) return kMaxBackupIntervalHours;
    int chosen = 0;
    for (int i = 0; i < kBackupIntervalOptionCount; ++i) {
        if (hours >= kBackupIntervalHours[i]) chosen = kBackupIntervalHours[i];
    }
    return chosen;
}

// 0 = 关闭（调用方据此停掉定时器），其余返回毫秒。
inline qint64 backupIntervalMs(int hours) {
    return qint64(normalizeBackupIntervalHours(hours)) * 60LL * 60LL * 1000LL;
}

} // namespace NexusAI
