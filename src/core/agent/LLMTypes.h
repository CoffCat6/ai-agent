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

    // docs/16 §5.1: 分类 token 计数（来自 API usage 对象）
    int promptTokens{0};           // 输入 token 合计（system + 历史 + tools + 当前轮）
    int completionTokens{0};       // 输出 token 合计（LLM 生成的 completion）
    int cachedTokens{0};           // 缓存命中 token（OpenAI: prompt_tokens_details.cached_tokens）
    int cacheReadTokens{0};        // 缓存读取（Anthropic: cache_read_input_tokens）
    int cacheCreationTokens{0};    // 缓存写入（Anthropic: cache_creation_input_tokens）
};

} // namespace NexusAI

Q_DECLARE_METATYPE(NexusAI::LLMResponse)
