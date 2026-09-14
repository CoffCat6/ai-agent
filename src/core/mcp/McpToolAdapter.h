#pragma once

#include "core/tools/ITool.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QWaitCondition>
#include <atomic>

#include <QString>

namespace NexusAI {

class McpClient;

// Bridge from one MCP server tool to the built-in ToolRegistry (Batch 31a).
// The adapter is registered as `mcp__<server>__<tool>` with a forced
// RequiresConfirm level and executes by issuing tools/call over its McpClient.
//
// Threading: execute() runs on a QtConcurrent pool thread (AIAgent's tool
// dispatch); the MCP response arrives on the GUI thread. The adapter waits
// on a mutex/condition pair with cancel-sliced waits — the same contract as
// shell_exec (ITool::cancel may only flip atomic flags; the pool thread
// observes them between slices).
class McpToolAdapter : public ITool {
public:
    McpToolAdapter(McpClient* client, const QString& serverName,
                   const QString& toolName, const QString& description,
                   const QJsonObject& inputSchema);

    // Registry-visible name for an MCP tool:
    // mcp__<sanitized server>__<sanitized tool>.
    static QString registryNameFor(const QString& server, const QString& tool);

    ToolDefinition definition() const override;
    ToolResult execute(const QJsonObject& args) override;
    void cancel() override;

private:
    McpClient* m_client{nullptr};
    QString m_serverName;
    QString m_toolName;
    QString m_description;
    QJsonObject m_inputSchema;
    QString m_registryName;
    std::atomic_bool m_cancelled{false};
};

} // namespace NexusAI
