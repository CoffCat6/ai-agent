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
};

} // namespace NexusAI

Q_DECLARE_METATYPE(NexusAI::LLMResponse)
