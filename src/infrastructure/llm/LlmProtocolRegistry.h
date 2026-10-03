#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

class QObject;

namespace NexusAI {

class IAIService;

// profile 的 apiProtocol 字段合法值；与持久化在 QSettings 里的字符串一致。
// 一个 key 就是一种线协议，与厂商（provider）是两个正交的轴。
namespace LlmProtocol {
inline constexpr auto kOpenaiChat = "openai-chat";
inline constexpr auto kOpenaiResponses = "openai-responses";
inline constexpr auto kAnthropicMessages = "anthropic-messages";
inline constexpr auto kOllamaChat = "ollama-chat";
}

// 一种线协议的自述：厂商归属、端点默认值、以及它的服务实现。
//
// 请求路径（如 /chat/completions）不在这里：路径属于报文构造，与各 Service
// 放在一起才不会两处漂移。这张表只回答"用哪个实现、该厂商的默认值是什么"。
struct LlmProtocolDescriptor {
    QString key;               // "openai-chat"
    QString provider;          // 厂商轴："openai"
    QString label;             // 协议名（技术名词，跨语言保持一致，不翻译）
    QString defaultBaseUrl;
    QString defaultModel;
    QStringList models;
    QHash<QString, int> contextWindows;   // model → 上下文窗口 token
    QString envApiKeyVar;      // 空 = 不读环境变量（本地服务）
    bool local{false};         // 内容不出本机，local-only 门控豁免
    IAIService* (*createService)(QObject* parent){nullptr};
};

// 协议 → 实现的唯一映射表。加一种协议只需要在表里加一条描述符：运行时装配、
// 连接测试与设置默认值都只查这张表，不再各自 if-else 判字符串。
class LlmProtocolRegistry {
public:
    // 注册顺序即候选展示顺序。
    static const QVector<LlmProtocolDescriptor>& descriptors();

    // 未注册返回 nullptr。返回的指针指向静态表，生命周期等于进程。
    static const LlmProtocolDescriptor* find(const QString& key);

    // 把任意（旧的、手改坏的）profile 输入归一到一个确定存在的描述符：
    // key 已知用 key；否则用该厂商的默认协议；再否则 openai-chat。
    // provider 与协议冲突时以协议为准——协议才决定线格式。返回值永不为空。
    static const LlmProtocolDescriptor* resolve(const QString& key,
                                                const QString& provider);

    static QStringList providers();   // 表里出现过的厂商，去重保序
    static bool isKnownProvider(const QString& provider);

    // 某厂商的全部协议，按注册顺序；第一条即该厂商的默认协议（也是 resolve
    // 的回落结果）。UI 的协议选择器用它列可选项。
    static QVector<LlmProtocolDescriptor> protocolsOfProvider(const QString& provider);

    // 给定（协议, 厂商）判定是否本地服务；main.cpp 的 local-only 门控用。
    static bool isLocal(const QString& key, const QString& provider);

    // 未注册 / 工厂缺失返回 nullptr，调用方据此报告"未配置模型服务"。
    static IAIService* createService(const QString& key, QObject* parent);

    // 装配表：每个已注册协议一个服务实例，parent 决定其生命周期。
    static QHash<QString, IAIService*> createServiceSet(QObject* parent);
};

} // namespace NexusAI
