#pragma once

#include "ITool.h"

namespace NexusAI {

// docs/35 P4.4: 结构化应用 spawn_agent（coder）产出的修改提案。
// 主代理不再抄写报告里的 find 原文去调 edit_file，而是用报告里的
// `Proposal ids` 调用本工具：预览与执行都从会话提案表取完整数据，
// 执行前重新读文件并重新校验锚点；写入仍走 RequiresConfirm 确认流，
// 成功后标记 applied，重复应用返回可读失败。
//
// 注册与 spawn_agent 同门控（main.cpp），子代理关闭时工具面不出现；
// 子代理自身的只读目录也绝不会包含它（RequiresConfirm）。
class ApplyProposalTool : public ITool {
public:
    static constexpr const char* kName = "apply_proposal";

    ToolDefinition definition() const override;
    ToolResult execute(const QJsonObject& args) override;
    // GUI 线程：从当前会话提案表取 find/replace，生成与 edit_file 相同
    // 形状的 unified diff，供确认弹窗预览；锚点失效时返回可读原因。
    QString generatePreview(const QJsonObject& args) const override;
};

} // namespace NexusAI