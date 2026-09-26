#pragma once

#include "ITool.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <memory>
#include <vector>

namespace NexusAI {

class ToolRegistry : public QObject {
    Q_OBJECT
public:
    static ToolRegistry& instance();

    void registerTool(std::unique_ptr<ITool> tool);
    ITool* find(const QString& name) const;
    // Batch 31a: removes the tool and transfers ownership to the caller —
    // dynamic MCP tools must outlive their registry entry while an in-flight
    // execute() may still be running on a pool thread.
    std::unique_ptr<ITool> takeTool(const QString& name);
    QVector<ToolDefinition> listAll() const;
    bool isRegistered(const QString& name) const;
    // Names of the tools the model is allowed to call (Dangerous excluded) -
    // used to expose the schema and to teach the model correct names when
    // it hallucinates one.
    QStringList toolNamesForModel() const;

    // Batch 14 (SEC-1401): disable a tool without removing it. Disabled
    // tools stay in listAll() (UI can still show they exist) but disappear
    // from toolNamesForModel() (no schema exposure) and calls that arrive
    // for them (e.g. stale history) are rejected with a structured error.
    void setToolEnabled(const QString& name, bool enabled);
    bool isToolEnabled(const QString& name) const;

    // T14 (REQ-T14-04): batch-enable/disable all tools whose name starts with
    // the given prefix (e.g. "mcp__" for all MCP tools). Used by the
    // local-only mode gate to disable all MCP tools at once.
    void setToolsEnabledByPrefix(const QString& prefix, bool enabled);

signals:
    // Fired on register/take/setEnabled — lets the UI refresh its tool
    // list when MCP servers connect/disconnect or the user toggles a switch.
    void toolsChanged();

private:
    ToolRegistry();
    void registerBuiltins();
    void loadDisabledFromSettings();
    void saveDisabledToSettings();

    // Store tools with unique ownership; use raw pointers for lookup
    std::vector<std::unique_ptr<ITool>> m_ownedTools;
    QHash<QString, ITool*> m_tools;
    QSet<QString> m_disabledTools;
};

} // namespace NexusAI
