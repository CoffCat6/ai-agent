#pragma once

#include "IAIService.h"

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSharedPointer>
#include <QElapsedTimer>
#include <QString>

class QNetworkRequest;

namespace NexusAI {

// Provider-specific per-request accumulation (StreamToolCall fragments etc.).
// Lives inside the request context so service objects keep no response state
// between requests at all.
struct ProviderRequestState {
    virtual ~ProviderRequestState() = default;
};
using ProviderRequestStatePtr = QSharedPointer<ProviderRequestState>;

// All state of ONE in-flight request. Created per chatComplete(); every
// callback captures a shared pointer and validates identity against the
// service's current context before touching anything, so a stale or
// cancelled reply can never pollute a newer request nor emit for it.
struct LLMRequestContext {
    quint64 requestId{0};
    QNetworkReply* reply{nullptr};
    // Retained so a transient failure can be retried transparently (REL-1001)
    QNetworkRequest request;
    QByteArray requestBody;
    int attempt{0};
    qint64 startMs{0};   // monotonic ms at (re)post, for network latency logs
    // streaming accumulators
    QByteArray lineBuffer;   // trailing incomplete line
    QByteArray rawBody;      // full raw response (for error diagnostics)
    QString content;
    QString reasoning;   // chain-of-thought accumulated on its own channel
    int tokensUsed{0};
    bool doneReceived{false};
    bool responseEmitted{false};
    bool errorEmitted{false};
    bool cancelled{false};
    ProviderRequestStatePtr provider;

    void resetStreamState() {
        lineBuffer.clear();
        rawBody.clear();
        content.clear();
        reasoning.clear();
        tokensUsed = 0;
        doneReceived = false;
        responseEmitted = false;
        errorEmitted = false;
    }
};
using LLMRequestContextPtr = QSharedPointer<LLMRequestContext>;

// Shared plumbing for the LLM providers: single in-flight request context
// with identity-validated callbacks, line-based streaming accumulation, and
// the finished-reply fallback ladder (streamed -> non-stream JSON body ->
// network error -> empty). Subclasses implement the wire protocol: how to
// build the request, how to parse one buffered line, and what an error /
// complete non-streamed body looks like for their API.
class LLMServiceBase : public IAIService {
    Q_OBJECT
public:
    explicit LLMServiceBase(QObject* parent = nullptr);
    ~LLMServiceBase() override;

    void cancel() override;
    void setApiKey(const QString& key) override { m_apiKey = key; }
    void setModel(const QString& model) override { m_model = model; }
    void setBaseUrl(const QString& url) override { m_baseUrl = url; }
    QString model() const override { return m_model; }

    // 端点三件套统一在这里落，协议差异全交给 applyProfileParams 这一个钩子，
    // 所以装配层（main.cpp）不需要再按协议字符串分支设参。
    void configure(const LlmProfileConfig& cfg) override;

protected:
    // 各协议从 profile 里取自己那部分参数；基类实现到此为止什么都不做。
    virtual void applyProfileParams(const LlmProfileConfig&) {}

    // POSTs the body as a NEW request context, emits requestStarted() and
    // wires readyRead/finished to identity-validated handlers.
    void startReply(QNetworkRequest& request, const QByteArray& body);

    // (Re-)posts an existing context (REL-1001 retry path keeps requestId).
    void postReply(const LLMRequestContextPtr& ctx);

    // ── Protocol hooks (operate on the request context) ──
    // Provider-specific per-request accumulation; default = none.
    virtual ProviderRequestStatePtr createRequestState() const
    { return ProviderRequestStatePtr::create(); }
    // One complete buffered line (no trailing newline). SSE services strip
    // their "data:" prefix here; NDJSON services parse the line directly.
    virtual void processLine(LLMRequestContext& ctx, const QByteArray& line) = 0;
    // True when the accumulated stream state forms a complete answer.
    virtual bool streamComplete(const LLMRequestContext& ctx) const = 0;
    // Assemble the accumulated stream state into the final response.
    virtual LLMResponse streamResult(const LLMRequestContext& ctx) const = 0;
    // Short provider word for error formatting ("API" / "Anthropic" / "Ollama").
    virtual QString providerLabel() const = 0;
    // Error payload inside a JSON body; empty when the body is no error.
    virtual QString extractApiError(const LLMRequestContext& ctx,
                                    const QJsonObject& json) const = 0;
    // True when the JSON body is a full (non-streamed) response.
    virtual bool isCompleteBody(const QJsonObject& json) const = 0;
    // Parse a complete body (wraps the testable static parser).
    virtual LLMResponse parseBody(const QJsonObject& json) const = 0;

    QString m_apiKey;
    QString m_model;
    QString m_baseUrl;

private:
    void onReadyRead(const LLMRequestContextPtr& ctx);
    void handleReplyFinished(const LLMRequestContextPtr& ctx);
    void processIncoming(const LLMRequestContextPtr& ctx);   // split lineBuffer into lines

    QNetworkAccessManager* m_nam;
    LLMRequestContextPtr m_current;   // the only context allowed to accumulate/emit
    quint64 m_lastRequestId{0};
    QElapsedTimer m_elapsed;   // network latency accounting (Activity logs)
};

} // namespace NexusAI
