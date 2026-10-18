#pragma once

#include "LLMServiceBase.h"

#include <QString>
#include <QVariant>
#include <QVector>

namespace NexusAI {

// Google Gemini GenerateContent service（任务 #24，批次四）。
//
// Agent 层统一用 OpenAI wire format（messages + tool_calls）；本类负责与
// Gemini v1beta REST 协议互转：
// - 请求 "tools": {type,function:{name,description,parameters}} ->
//   [{"functionDeclarations":[{name,description,parameters}]}]
// - 历史: assistant tool_calls -> model content 的 functionCall parts
//   （OpenAI 的 arguments 是 JSON 字符串，Gemini 的 args 要解析后的对
//   象）；role:"tool" 结果 -> user content 的 functionResponse parts
//   （连续多条合并进同一 user 轮；Gemini 没有 id 关联，函数名从历史
//   assistant tool_calls 按 tool_call_id 反查）
// - role:"system" 折叠进顶层 systemInstruction（参照 Anthropic 的折
//   叠，多个 system 用空行连接）
// - 图片：user 消息的 "images" data URL 数组 -> inlineData parts
// - 响应: parts 里的 {text} 进 content、{thought:true,text} 走
//   reasoning 独立通道、{functionCall} 累积为 ToolCallInfo（Gemini 不
//   返回调用 id，统一合成 "gemini-call-{序号}" 保住 wire format 兼容）
//
// 流式端点 POST {base}/v1beta/models/{model}:streamGenerateContent?alt=sse
// （SSE 响应）；鉴权走 x-goog-api-key 请求头——不用 ?key= 查询参数，
// key 会随 URL 整串进日志与代理。所有请求/流式状态都在 per-request
// LLMRequestContext 里，Service 对象本身只携带配置。
class GeminiService : public LLMServiceBase {
    Q_OBJECT
public:
    explicit GeminiService(QObject* parent = nullptr);

    void chatComplete(const QJsonArray& messages, const QJsonArray& tools = {}) override;

    // 采样参数 opt-in（仿 AnthropicService）：temperature 未设置不发字
    // 段；maxOutputTokens 未设置保持 4096 兜底（Gemini 非必填，但兜底
    // 与兄弟后端行为一致，也避免思考档位下模型无限制输出）。
    void setSamplingParams(const QVariant& temperature, const QVariant& maxOutputTokens);

    // 思考强度档位（"low"|"medium"|"high"；"off"/未设置 = 不启用）。
    void setReasoningEffort(const QVariant& effort);

    // 档位 -> generationConfig.thinkingConfig.thinkingBudget，0 = 不发
    // thinkingConfig 字段（服务端默认行为）。纯函数，供测试直接断言。
    // 档位与 Anthropic 对齐；16384 在 gemini-2.5-flash（上限 24576）与
    // pro（上限 32768）两侧都合法。
    // s-1 现状登记：off 档位返回 0 = 不指定 thinkingConfig，交由服务端
    // 默认动态思考——而非"彻底关闭思考"。Gemini 2.5/3 系列本身就是
    // thinking 模型，API 没有真正意义上的"关思考"开关，只能"不指定"。
    static int thinkingBudgetFor(const QString& level);

    // M-1（任务 #27）：Gemini 的 maxOutputTokens 是包含思考 token 的输
    // 出总预算，thinkingBudget 超过它时 API 直接 400——档位必须按输出
    // 上限钳制（仿 AnthropicService 的折算降级防护）：
    // budget <= maxOutputTokens - kMinAnswerTokens；留不出最低预算时
    // 降级为 0（不发 thinkingConfig）并 qWarning。纯函数，供测试断言。
    static int clampedThinkingBudget(const QString& level, int maxOutputTokens);

    // s-3（任务 #27）：拦截语义的 finishReason 家族——SAFETY/RECITATION/
    // PROHIBITED/BLOCKLIST/IMAGE_SAFETY/SPII（内容被安全/合规过滤，零有
    // 效输出）。非流式 parseResponse 与流式 streamResult 两条路径共用，
    // 零输出时统一升级为错误而非"成功但空内容"。
    static bool isBlockedFinishReason(const QString& reason);

    // m-2（任务 #27）：模型名拼进 URL path 的一环，只放行
    // [A-Za-z0-9._:~-]——?/#//空白会破坏 URL 结构（截断 query、改写
    // path、被 percent-encode 成 %20）。先 trim；非法或为空返回空串，
    // 调用方需提前报错放弃请求。
    static QString sanitizeModelName(const QString& model);

    // m-2（任务 #27）：{base}/v1beta/models/{model}:streamGenerateContent
    // ?alt=sse；baseUrl 末尾多余的 "/" 去重（避免 "//v1beta"）。纯函数
    // 供测试直接断言。
    static QString buildStreamUrl(const QString& baseUrl, const QString& model);

    // Pure conversion / parsing helpers (no network state); exposed for tests.
    struct ConvertedRequest {
        QString systemInstruction;
        QJsonArray contents;
    };
    static ConvertedRequest convertMessages(const QJsonArray& messages);
    static LLMResponse parseResponse(const QJsonObject& json);

    // OpenAI wire format {type:"function",function:{name,description,parameters}}
    // -> Gemini [{"functionDeclarations":[{"name",description,parameters}]}]。
    // parameters 走 sanitizeSchema 子集降级；无法安全降级的工具整条跳过。
    static QJsonArray convertTools(const QJsonArray& tools);

    // m-4：MCP inputSchema 是完整 JSON Schema，可能包含 Gemini Schema 子集不
    // 支持的 $ref/$defs/anyOf/oneOf/allOf 等关键字，一旦发出去整条工具声明
    // 直接 400。这里做子集校验/降级：递归裁剪不支持关键字、把 anyOf/oneOf/
    // allOf 收敛到第一个可降级分支、顶层收敛为 object+properties。无法安全
    // 降级（如顶层是明确的非对象标量/数组且无 properties 可收敛）时 *ok=false
    // 并返回空对象，调用方应跳过该工具并 qWarning。纯函数，供测试断言。
    static QJsonObject sanitizeSchema(const QJsonObject& inputSchema, bool* ok);

protected:
    struct GeminiRequestState : ProviderRequestState {
        // Gemini 的 functionCall 在单个 chunk 里完整给出（不分片），直
        // 接累积成品 ToolCallInfo；id 按序合成。
        QVector<ToolCallInfo> toolCalls;
        // usageMetadata 在每个 chunk 上都是"迄今为止"的累积值，覆盖
        // 赋值，最后一个 chunk 即最终计数。
        int promptTokens{0};
        int completionTokens{0};
        int thoughtsTokens{0};
        // 拦截型 finishReason（SAFETY/PROHIBITED/... 全家族见
        // isBlockedFinishReason）且无任何可用输出时，streamResult 要把
        // 空响应升级为错误——这里记住最后一个 finishReason。
        QString finishReason;
        // m-3: functionCall 按 chunk 逐个 append，重试必须清空否则重复。
        void reset() override { toolCalls.clear(); finishReason.clear(); }
    };

    ProviderRequestStatePtr createRequestState() const override
    { return ProviderRequestStatePtr(new GeminiRequestState()); }
    void processLine(LLMRequestContext& ctx, const QByteArray& line) override;
    bool streamComplete(const LLMRequestContext& ctx) const override;
    LLMResponse streamResult(const LLMRequestContext& ctx) const override;
    QString providerLabel() const override { return QStringLiteral("Gemini"); }
    QString extractApiError(const LLMRequestContext& ctx,
                            const QJsonObject& json) const override;
    bool isCompleteBody(const QJsonObject& json) const override;
    LLMResponse parseBody(const QJsonObject& json) const override
    { return parseResponse(json); }

    // GenerateConfig 只读输出上限与思考档位（numCtx 是 Ollama 的上下文
    // 长度概念，Gemini 用不到）。
    void applyProfileParams(const LlmProfileConfig& cfg) override
    {
        setSamplingParams(cfg.temperature, cfg.maxOutputTokens);
        setReasoningEffort(cfg.reasoningEffort);
    }

private:

    int m_maxOutputTokens{4096};   // 4096 兜底（对齐 Anthropic 的 max_tokens 兜底）
    QVariant m_temperature;        // 未设置 → generationConfig 不发该字段
    QVariant m_reasoningEffort;    // 未设置 → 不发 thinkingConfig
};

} // namespace NexusAI