#include "AIAgent.h"
#include "infrastructure/llm/IAIService.h"
#include "core/tools/ToolRegistry.h"
#include "core/memory/IMemory.h"

#include <QDateTime>
#include <QUuid>

namespace NexusAI {

AIAgent::AIAgent(QObject* parent) : QObject(parent) {}

AIAgent::~AIAgent() = default;

void AIAgent::setLLMService(IAIService* service) {
    m_llmService = service;
}

void AIAgent::setToolRegistry(ToolRegistry* registry) {
    m_toolRegistry = registry;
}

void AIAgent::setMemory(IMemory* memory) {
    m_memory = memory;
}

void AIAgent::setState(AgentState state) {
    if (m_state == state) return;
    m_state = state;
    emit stateChanged(m_state);
}

void AIAgent::appendTraceNode(const QString& type, const QString& title,
                               const QString& status, const QJsonObject& extra) {
    QJsonObject node;
    node["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    node["type"] = type;
    node["title"] = title;
    node["status"] = status;
    node["timestamp"] = QDateTime::currentDateTime().toMSecsSinceEpoch();
    node["args"] = extra.value("args").toString();
    node["result"] = extra.value("result").toString();

    m_traceModel.append(node);
    emit traceChanged();
}

void AIAgent::startTask(const QString& userMessage) {
    if (m_state != AgentState::Idle && m_state != AgentState::Completed
        && m_state != AgentState::Error) {
        return;
    }

    m_messages = QJsonArray();
    m_traceModel = QJsonArray();
    m_iteration = 0;

    // Add user message
    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = userMessage;
    m_messages.append(userMsg);

    appendTraceNode("user_message", userMessage, "completed");

    setState(AgentState::Thinking);
    processIteration();
}

void AIAgent::processIteration() {
    if (!m_llmService) {
        setState(AgentState::Error);
        emit errorOccurred("No LLM service configured");
        return;
    }

    appendTraceNode("thinking", "Analyzing request...", "running");

    // Connect to LLM response (will be called when IAIService emits responseReady)
    connect(m_llmService, &IAIService::responseReady, this,
            [this](const LLMResponse& response) {
        // Update the last thinking node
        if (!m_traceModel.isEmpty()) {
            QJsonObject lastNode = m_traceModel.last().toObject();
            lastNode["status"] = "completed";
            m_traceModel.replace(m_traceModel.size() - 1, lastNode);
            emit traceChanged();
        }

        if (!response.success) {
            setState(AgentState::Error);
            emit errorOccurred(response.error);
            return;
        }

        // Add assistant message to history
        QJsonObject assistantMsg;
        assistantMsg["role"] = "assistant";
        assistantMsg["content"] = response.content;
        m_messages.append(assistantMsg);

        if (response.toolCalls.isEmpty()) {
            // No tool calls - we have the final response
            setState(AgentState::Summarizing);
            appendTraceNode("summary", response.content, "completed");
            setState(AgentState::Completed);
            emit responseReady(response.content);
        } else {
            // Process tool calls
            for (const auto& call : response.toolCalls) {
                appendTraceNode("tool_call", call.name, "running",
                    QJsonObject{{"args", QString(QJsonDocument(call.arguments).toJson(QJsonDocument::Compact))}});

                // For now, mark tool as completed
                if (!m_traceModel.isEmpty()) {
                    QJsonObject lastNode = m_traceModel.last().toObject();
                    lastNode["status"] = "success";
                    m_traceModel.replace(m_traceModel.size() - 1, lastNode);
                    emit traceChanged();
                }
            }

            m_iteration++;
            if (m_iteration < m_maxIterations) {
                setState(AgentState::Thinking);
                processIteration();
            } else {
                setState(AgentState::Completed);
                emit responseReady("Max iterations reached.");
            }
        }
    }, Qt::SingleShotConnection);

    // Trigger the LLM call
    QJsonArray toolsArray;
    if (m_toolRegistry) {
        for (const auto& def : m_toolRegistry->listAll()) {
            QJsonObject toolObj;
            toolObj["type"] = "function";
            QJsonObject func;
            func["name"] = def.name;
            func["description"] = def.description;
            func["parameters"] = def.parameters;
            toolObj["function"] = func;
            toolsArray.append(toolObj);
        }
    }

    m_llmService->chatComplete(m_messages, toolsArray);
}

void AIAgent::approveToolCall(const QString& id, bool approved) {
    // Find the tool call node in trace
    for (int i = 0; i < m_traceModel.size(); ++i) {
        QJsonObject node = m_traceModel.at(i).toObject();
        if (node["id"].toString() == id) {
            node["status"] = approved ? "success" : "cancelled";
            m_traceModel.replace(i, node);
            emit traceChanged();
            break;
        }
    }

    if (approved) {
        setState(AgentState::ExecutingTool);
        // Continue processing
        m_iteration++;
        if (m_iteration < m_maxIterations) {
            setState(AgentState::Thinking);
            processIteration();
        } else {
            setState(AgentState::Completed);
        }
    }
}

void AIAgent::cancel() {
    setState(AgentState::Idle);
    m_traceModel = QJsonArray();
    emit traceChanged();
}

} // namespace NexusAI
