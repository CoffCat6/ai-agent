#pragma once

#include "core/tools/ITool.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QMutex>
#include <QPointer>
#include <QWaitCondition>
#include <atomic>

#include <QString>

namespace NexusAI {

class McpClient;

// Bridge from one MCP server tool to the built-in ToolRegistry (Batch 31a).
// The adapter is registered as `mcp__<server>__<tool>` with a forced
// RequiresConfirm level and executes by issuing tools/call over its McpClient.
//
class McpToolAdapter : public ITool {
public:
    // T14 (REQ-T14-02): max characters of tool description exposed to the model.
    // Untrusted third-party text is hard-capped before it enters the context.
    static constexpr int MaxDescriptionChars = 1024;
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
    // Guarded: an in-flight execute() on the pool thread can outlive the
    // client (server removed mid-call, docs/22 F1) — the tool itself stays
    // alive via the dispatch shared_ptr, the client must be checked.
    QPointer<McpClient> m_client{nullptr};
    QString m_serverName;
    QString m_toolName;
    QString m_description;
    QJsonObject m_inputSchema;
    QString m_registryName;
    std::atomic_bool m_cancelled{false};
};

} // namespace NexusAI
