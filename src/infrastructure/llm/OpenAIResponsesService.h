#pragma once

#include "LLMServiceBase.h"

#include <QString>
#include <QVariant>
#include <QVector>

namespace NexusAI {

// OpenAI Responses API (/responses) service with tool calling and SSE.
//
// 与 Chat Completions 的差异全部收口在这一层：agent 侧依旧用 OpenAI Chat 报文
// 当中间表示（Anthropic 也是这么对接的）。
// - 请求：messages → input item 数组；system/developer 抽成顶层 instructions；
//   assistant.tool_calls → {"type":"function_call"} item，role:"tool" 结果 →
//   {"type":"function_call_output"} item，两者靠 call_id 精确配对（漏一个或
//   对不上，API 直接 400）；tools 是扁平的 {type,name,description,parameters}，
//   没有 Chat 那层 function 包装。
// - 响应：每条 SSE 的 data JSON 自带 "type"，所以按 type 分派、完全不解析
//   event: 行——官方 SDK 同样做法，第三方网关的 event 行更不可靠。
// - 会话：store=false，每轮全量重发 input。不引入 previous_response_id：那会
//   和 AIAgent 的上下文裁剪与 token 计费口径打架。
//
// 已知取舍：思考模型带工具调用时，官方无状态用法要求把 reasoning item（含
// encrypted_content）一并回传。本项目的历史报文里没有存放它的通道，所以不
// 回传——推理在轮与轮之间不连续，但请求本身合法。要补就得动 LLMResponse 与
// SqliteMemory 的历史结构，属于独立一步。
class OpenAIResponsesService : public LLMServiceBase {
    Q_OBJECT
public:
    explicit OpenAIResponsesService(QObject* parent = nullptr);

    void chatComplete(const QJsonArray& messages, const QJsonArray& tools = {}) override;

    // 与 Chat Completions 同一套"未设置就不发字段"语义。max_output_tokens 有
    // 下限（含推理 token），过小会直接 incomplete，所以按 API 下限钳制。
    void setSamplingParams(const QVariant& temperature, const QVariant& maxOutputTokens);

    // 思考强度档位：Responses 的 reasoning.effort 直接吃档位字符串，
    // "off" 对应线上取值 "none"（枚举里没有 off）。
    void setReasoningEffort(const QVariant& effort);

    // 档位 -> 线上取值；未知/空返回空串表示不发 reasoning 字段。纯函数供测试断言。
    static QString reasoningEffortParam(const QString& level);

    // ── 报文转换与解析（纯函数，供单元测试直接断言）──
    static QJsonArray convertMessagesToInput(const QJsonArray& messages);
    static QString instructionsFromMessages(const QJsonArray& messages);
    static QJsonArray convertTools(const QJsonArray& tools);
    static LLMResponse parseResponse(const QJsonObject& json);

protected:
    // 一个 function_call item 的跨事件累积；按 item_id（或 output_index）归位。
    struct PendingCall {
        QString key;
        QString callId;
        QString name;
        QString arguments;   // JSON 字符串
    };
    struct ResponsesRequestState : ProviderRequestState {
        QVector<PendingCall> calls;
        QHash<QString, int> indexByKey;
        // response.completed 里的 usage 对象；为空则 streamResult 退回估算路径。
        QJsonObject usage;
    };

    ProviderRequestStatePtr createRequestState() const override
    { return ProviderRequestStatePtr(new ResponsesRequestState()); }
    void processLine(LLMRequestContext& ctx, const QByteArray& line) override;
    bool streamComplete(const LLMRequestContext& ctx) const override;
    LLMResponse streamResult(const LLMRequestContext& ctx) const override;
    QString providerLabel() const override { return QStringLiteral("Responses"); }
    QString extractApiError(const LLMRequestContext& ctx,
                            const QJsonObject& json) const override;
    bool isCompleteBody(const QJsonObject& json) const override;
    LLMResponse parseBody(const QJsonObject& json) const override
    { return parseResponse(json); }

    // Responses 与 Chat Completions 消费同一组 profile 参数（输出上限 + 思考
    // 档位），只有字段名和下限不同。
    void applyProfileParams(const LlmProfileConfig& cfg) override
    {
        setSamplingParams(cfg.temperature, cfg.maxOutputTokens);
        setReasoningEffort(cfg.reasoningEffort);
    }

protected:
    QJsonObject buildRequestBody(const QJsonArray& messages, const QJsonArray& tools);

    // Responses 流式事件里定位一个 function_call：item_id 优先，缺它才退到
    // output_index（部分网关不带 item_id）。
    static QString callKey(const QJsonObject& json, const QJsonObject& item);

    // 按 key 取（不存在则按到达顺序追加）一个 function_call 累积槽。
    static PendingCall& slotFor(ResponsesRequestState& state, const QString& key);

    // 累积槽 → agent 用的 ToolCallInfo（缺 name 的半截 item 被丢弃）。
    static QVector<ToolCallInfo> finalizeCalls(const QVector<PendingCall>& calls);

    QVariant m_temperature;
    QVariant m_maxOutputTokens;
    QVariant m_reasoningEffort;
};

} // namespace NexusAI
