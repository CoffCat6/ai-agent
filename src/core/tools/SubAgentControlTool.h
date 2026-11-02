#pragma once

#include "ITool.h"

namespace NexusAI {

// docs/35 P4.3: 后台子代理的管理工具（仅在子代理启用时与 spawn_agent
// 一起注册，且从子代理目录排除）。三个工具都从线程局部
// BackgroundTaskHubRouter 取会话任务表；不在 agent 会话内时拒绝。
class SubAgentWaitTool : public ITool {
public:
    static constexpr const char* kName = "subagent_wait";
    ToolDefinition definition() const override;
    ToolResult execute(const QJsonObject& args) override;
    ToolResult execute(const QJsonObject& args,
                       const Cancellation& cancelled) override;
};

class SubAgentStatusTool : public ITool {
public:
    static constexpr const char* kName = "subagent_status";
    ToolDefinition definition() const override;
    ToolResult execute(const QJsonObject& args) override;
};

class SubAgentCancelTool : public ITool {
public:
    static constexpr const char* kName = "subagent_cancel";
    ToolDefinition definition() const override;
    ToolResult execute(const QJsonObject& args) override;
};

} // namespace NexusAI