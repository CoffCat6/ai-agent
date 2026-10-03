#include "LlmProtocolRegistry.h"

#include "AnthropicService.h"
#include "IAIService.h"
#include "OllamaService.h"
#include "OpenAIResponsesService.h"
#include "OpenAIService.h"

namespace NexusAI {

namespace {
QString protocolKey(const char* key) { return QString::fromLatin1(key); }
}

const QVector<LlmProtocolDescriptor>& LlmProtocolRegistry::descriptors() {
    static const QVector<LlmProtocolDescriptor> table = {
        {protocolKey(LlmProtocol::kOpenaiChat), QStringLiteral("openai"),
         QStringLiteral("Chat Completions"),
         QStringLiteral("https://api.openai.com/v1"), QStringLiteral("gpt-4o-mini"),
         {QStringLiteral("gpt-4o-mini"), QStringLiteral("gpt-4o"),
          QStringLiteral("gpt-4-turbo"), QStringLiteral("o3-mini")},
         {{QStringLiteral("gpt-4o-mini"), 128000},
          {QStringLiteral("gpt-4o"), 128000},
          {QStringLiteral("gpt-4-turbo"), 128000},
          {QStringLiteral("o3-mini"), 200000}},
         QStringLiteral("OPENAI_API_KEY"), false,
         [](QObject* parent) -> IAIService* { return new OpenAIService(parent); }},

        // OpenAI 官方端点；绝大多数"OpenAI 兼容"网关只实现 /chat/completions，
        // 所以这条协议排在 openai-chat 之后——厂商默认协议取表里第一条。
        {protocolKey(LlmProtocol::kOpenaiResponses), QStringLiteral("openai"),
         QStringLiteral("Responses"),
         QStringLiteral("https://api.openai.com/v1"), QStringLiteral("gpt-4o-mini"),
         {QStringLiteral("gpt-4o-mini"), QStringLiteral("gpt-4o"),
          QStringLiteral("o3-mini"), QStringLiteral("gpt-4.1")},
         {{QStringLiteral("gpt-4o-mini"), 128000},
          {QStringLiteral("gpt-4o"), 128000},
          {QStringLiteral("o3-mini"), 200000},
          {QStringLiteral("gpt-4.1"), 1000000}},
         QStringLiteral("OPENAI_API_KEY"), false,
         [](QObject* parent) -> IAIService* { return new OpenAIResponsesService(parent); }},

        {protocolKey(LlmProtocol::kAnthropicMessages), QStringLiteral("anthropic"),
         QStringLiteral("Messages"),
         QStringLiteral("https://api.anthropic.com"), QStringLiteral("claude-sonnet-4-5"),
         {QStringLiteral("claude-sonnet-4-5"), QStringLiteral("claude-3-5-haiku-20241022"),
          QStringLiteral("claude-3-5-sonnet-20241022"), QStringLiteral("claude-3-opus-20240229")},
         {{QStringLiteral("claude-sonnet-4-5"), 200000},
          {QStringLiteral("claude-3-5-haiku-20241022"), 200000},
          {QStringLiteral("claude-3-5-sonnet-20241022"), 200000},
          {QStringLiteral("claude-3-opus-20240229"), 200000}},
         QStringLiteral("ANTHROPIC_API_KEY"), false,
         [](QObject* parent) -> IAIService* { return new AnthropicService(parent); }},

        {protocolKey(LlmProtocol::kOllamaChat), QStringLiteral("ollama"),
         QStringLiteral("Ollama Chat"),
         QStringLiteral("http://localhost:11434"), QStringLiteral("qwen2.5:7b"),
         {QStringLiteral("qwen2.5:7b"), QStringLiteral("llama3.1:8b"),
          QStringLiteral("deepseek-r1:7b")},
         {{QStringLiteral("qwen2.5:7b"), 32768},
          {QStringLiteral("llama3.1:8b"), 128000},
          {QStringLiteral("deepseek-r1:7b"), 65536}},
         QString(), true,
         [](QObject* parent) -> IAIService* { return new OllamaService(parent); }},
    };
    return table;
}

const LlmProtocolDescriptor* LlmProtocolRegistry::find(const QString& key) {
    for (const auto& d : descriptors()) {
        if (d.key == key) return &d;
    }
    return nullptr;
}

const LlmProtocolDescriptor* LlmProtocolRegistry::resolve(const QString& key,
                                                          const QString& provider) {
    if (const auto* exact = find(key)) return exact;
    for (const auto& d : descriptors()) {
        if (d.provider == provider) return &d;
    }
    return find(protocolKey(LlmProtocol::kOpenaiChat));
}

QStringList LlmProtocolRegistry::providers() {
    QStringList out;
    for (const auto& d : descriptors()) {
        if (!out.contains(d.provider)) out.append(d.provider);
    }
    return out;
}

bool LlmProtocolRegistry::isKnownProvider(const QString& provider) {
    for (const auto& d : descriptors()) {
        if (d.provider == provider) return true;
    }
    return false;
}

QVector<LlmProtocolDescriptor>
LlmProtocolRegistry::protocolsOfProvider(const QString& provider) {
    QVector<LlmProtocolDescriptor> out;
    for (const auto& d : descriptors()) {
        if (d.provider == provider) out.append(d);
    }
    return out;
}

bool LlmProtocolRegistry::isLocal(const QString& key, const QString& provider) {
    const auto* d = resolve(key, provider);
    return d && d->local;
}

IAIService* LlmProtocolRegistry::createService(const QString& key, QObject* parent) {
    const auto* d = find(key);
    if (!d || !d->createService) return nullptr;
    return d->createService(parent);
}

QHash<QString, IAIService*> LlmProtocolRegistry::createServiceSet(QObject* parent) {
    QHash<QString, IAIService*> set;
    for (const auto& d : descriptors()) {
        if (auto* service = createService(d.key, parent)) set.insert(d.key, service);
    }
    return set;
}

} // namespace NexusAI
