#pragma once

#include "ITool.h"

namespace NexusAI {

// docs/34 P3.1：子代理的「写意图」出口——只提出结构化修改提案，**永不落盘**。
//
// 为什么是工具而不是写权限：子代理跑在池线程、有自己的事件循环，而写工具
// （file_write/edit_file）是 RequiresConfirm，确认发生在主代理的 GUI 线程。
// 让确认冒泡进子代理需要池线程挂起等待 GUI 决策，会把已经修好的中止/超时
// 路径重新拉进竞态面（docs/32 §8）。提案模式把「起草」与「落盘」分开：
// 子代理只产提案，主代理用现有写工具应用，用户确认气泡与 diff 预览原样复用。
//
// 工具本身是 Safe + readOnly（校验只读文件、不写任何字节），因此天然落在
// 子代理只读目录的过滤规则内；但它是「能力」而非普通读工具——只有角色
// 白名单显式点名 propose_edit 时才授予（见 SubAgentTool::execute 的能力门）。
class ProposeEditTool : public ITool {
public:
    static constexpr const char* kName = "propose_edit";

    // 配额（docs/34 §5）：单次调用 / 单次运行 / 单条 find / 单条 replace /
    // 单文件大小。全部超限即失败（可读原因），不截断——截断会产出半个补丁。
    static constexpr int kMaxEditsPerCall = 3;
    static constexpr int kMaxProposalsPerRun = 20;
    static constexpr int kMaxFindChars = 4000;
    static constexpr int kMaxReplaceChars = 8 * 1024;
    static constexpr qint64 kMaxFileBytes = 1024 * 1024;

    ToolDefinition definition() const override;
    ToolResult execute(const QJsonObject& args) override;
};

} // namespace NexusAI
