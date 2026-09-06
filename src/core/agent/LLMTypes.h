#pragma once

#include <QString>
#include <QVector>

#include "core/tools/ITool.h"

namespace NexusAI {

// LLM response structure (domain type shared by core & infrastructure)
struct LLMResponse {
    QString content;
    QVector<ToolCallInfo> toolCalls;
    int tokensUsed{0};
    bool success{false};
    QString error;
    // Echoes the requestStarted() id of the request this response belongs to;
    // 0 = unknown (e.g. scripted fakes that don't track request ids).
    quint64 requestId{0};
};

} // namespace NexusAI

Q_DECLARE_METATYPE(NexusAI::LLMResponse)
