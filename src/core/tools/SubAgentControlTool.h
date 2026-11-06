#pragma once

#include "ITool.h"

#include <memory>

namespace NexusAI {

class SubAgentTool;

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

    // docs/43 O4 审计收口（P1-3）: execute() 跑在 QtConcurrent 池线程，
    // 禁止直查无锁的 ToolRegistry（GUI 线程动态注册/注销工具时并发读写
    // QHash 是 UB）。创建处（GUI 线程）直接注入共享 spawn 工具引用；
    // gateSummary() 自身由 m_gateMutex 保护，跨线程调用安全。
    void setSpawnTool(const std::shared_ptr<SubAgentTool>& tool);

private:
    std::weak_ptr<SubAgentTool> m_spawnTool;
};

class SubAgentCancelTool : public ITool {
public:
    static constexpr const char* kName = "subagent_cancel";
    ToolDefinition definition() const override;
    ToolResult execute(const QJsonObject& args) override;
};

} // namespace NexusAI