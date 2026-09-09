#pragma once

#include "ITool.h"

#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <memory>
#include <vector>

namespace NexusAI {

class ToolRegistry {
public:
    static ToolRegistry& instance();

    void registerTool(std::unique_ptr<ITool> tool);
    ITool* find(const QString& name) const;
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

private:
    ToolRegistry();
    void registerBuiltins();

    // Store tools with unique ownership; use raw pointers for lookup
    std::vector<std::unique_ptr<ITool>> m_ownedTools;
    QHash<QString, ITool*> m_tools;
    QSet<QString> m_disabledTools;
};

} // namespace NexusAI
