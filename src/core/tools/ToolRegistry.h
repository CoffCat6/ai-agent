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
    // 这是"用户禁用"层：持久化到 tools/disabled（QSettings）。
    void setToolEnabled(const QString& name, bool enabled);
    bool isToolEnabled(const QString& name) const;

    // docs/17 B1: 策略拦截层（内存态，不持久化）——与用户禁用分离。
    // localOnly 等隐私门控写这里，绝不触碰 tools/disabled 持久层，
    // 策略启停因此不会覆写用户手动禁用的开关。
    void setPolicyBlocked(const QStringList& names, bool blocked);
    bool isPolicyBlocked(const QString& name) const;
    // 最终可用性：用户未禁用 且 未被策略拦截。模型 schema 暴露与调用
    // 分支一律以本判定为准。
    bool isToolUsable(const QString& name) const;

signals:
    // Fired on register/take/setEnabled/policy change — lets the UI refresh
    // its tool list when MCP servers connect/disconnect or the user toggles
    // a switch.
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
    QSet<QString> m_policyBlocked;   // docs/17 B1: 运行时策略拦截，不落盘
};

} // namespace NexusAI
