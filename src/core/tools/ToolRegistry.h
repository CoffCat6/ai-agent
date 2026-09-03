#pragma once

#include "ITool.h"

#include <QHash>
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

private:
    ToolRegistry();
    void registerBuiltins();

    // Store tools with unique ownership; use raw pointers for lookup
    std::vector<std::unique_ptr<ITool>> m_ownedTools;
    QHash<QString, ITool*> m_tools;
};

} // namespace NexusAI
