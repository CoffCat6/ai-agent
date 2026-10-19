#pragma once

#include <QString>

namespace NexusAI {

// 会话级权限模式（docs：会话中的权限管理）。每个 tab 的 AIAgent 持有一份，
// 决定 processNextToolCall 中 RequiresConfirm 级工具是否需要逐次人工确认。
// 内存态、不持久化：tab 关闭/应用重启后回落到全局默认（设置键
// agent/defaultPermissionMode），完全访问不会跨会话残留。
enum class PermissionMode {
    ConfirmChanges,   // 变更确认：所有确认级工具逐次确认（历史行为）
    AutoEdit,         // 自动编辑：autoEditSafe 工具（文件编辑/搜索抓取）
                      // 自动放行，shell_exec 与 MCP 工具仍逐次确认
    FullAccess,       // 完全访问：确认级工具全部自动放行（Dangerous 除外——
                      // 任何模式下 Dangerous 都不自动执行）
    PlanOnly          // 只读/计划：Safe 工具与显式 readOnly 的确认级工具
                      // （联网检索）可用；其余 RequiresConfirm 工具——
                      // 文件写入、命令、提交、MCP——直接拒绝并告知模型
};

// 设置持久化用的稳定字符串形式（SettingsService 存取）。
inline QString permissionModeToString(PermissionMode mode) {
    switch (mode) {
        case PermissionMode::AutoEdit:    return QStringLiteral("auto_edit");
        case PermissionMode::FullAccess:  return QStringLiteral("full_access");
        case PermissionMode::PlanOnly:    return QStringLiteral("plan_only");
        case PermissionMode::ConfirmChanges: break;
    }
    return QStringLiteral("confirm");
}

inline PermissionMode permissionModeFromString(const QString& s) {
    if (s == QLatin1String("auto_edit"))   return PermissionMode::AutoEdit;
    if (s == QLatin1String("full_access")) return PermissionMode::FullAccess;
    if (s == QLatin1String("plan_only"))   return PermissionMode::PlanOnly;
    return PermissionMode::ConfirmChanges;
}

// QML/ViewModel 以 int 传递模式（属性常量）。未知值一律拒绝，避免
// static_cast 出枚举范围后让权限判定落入意外分支。
inline bool isValidPermissionMode(int mode) {
    switch (static_cast<PermissionMode>(mode)) {
        case PermissionMode::ConfirmChanges:
        case PermissionMode::AutoEdit:
        case PermissionMode::FullAccess:
        case PermissionMode::PlanOnly:
            return true;
    }
    return false;
}

} // namespace NexusAI
