#pragma once

#include <QObject>

namespace NexusAI {

// Agent lifecycle states - drives the state machine
enum class AgentState {
    Idle,               // Waiting for user input
    Thinking,           // LLM is processing
    AwaitingToolConfirm,// Waiting for user to approve a tool call
    ExecutingTool,      // Tool is running
    Summarizing,        // Generating final response
    Completed,          // Task finished successfully
    Error               // An error occurred
};

} // namespace NexusAI

// Register enum for QML usage
Q_DECLARE_METATYPE(NexusAI::AgentState)
