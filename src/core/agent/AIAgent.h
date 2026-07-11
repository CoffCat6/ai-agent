#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <memory>

#include "AgentState.h"
#include "Task.h"
#include "core/tools/ITool.h"

namespace NexusAI {

// Forward declarations
class IAIService;
class ToolRegistry;
class IMemory;

class AIAgent : public QObject {
    Q_OBJECT
public:
    explicit AIAgent(QObject* parent = nullptr);
    ~AIAgent() override;

    void setLLMService(IAIService* service);
    void setToolRegistry(ToolRegistry* registry);
    void setMemory(IMemory* memory);

    AgentState state() const { return m_state; }
    QJsonArray messages() const { return m_messages; }
    QJsonArray traceModel() const { return m_traceModel; }

public slots:
    void startTask(const QString& userMessage);
    void approveToolCall(const QString& id, bool approved);
    void cancel();

signals:
    void stateChanged(AgentState state);
    void traceChanged();
    void responseReady(const QString& response);
    void toolConfirmRequired(const QString& id, const QString& name, const QJsonObject& args);
    void errorOccurred(const QString& error);

private:
    void setState(AgentState state);
    void appendTraceNode(const QString& type, const QString& title,
                         const QString& status, const QJsonObject& extra = {});
    void processIteration();

    AgentState m_state{AgentState::Idle};
    QJsonArray m_messages;
    QJsonArray m_traceModel;
    int m_iteration{0};
    int m_maxIterations{10};

    IAIService* m_llmService{nullptr};
    ToolRegistry* m_toolRegistry{nullptr};
    IMemory* m_memory{nullptr};

    // Pending tool call awaiting approval
    QString m_pendingToolCallId;
};

} // namespace NexusAI
