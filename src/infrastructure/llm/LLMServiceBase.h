#pragma once

#include "IAIService.h"

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>

class QNetworkRequest;

namespace NexusAI {

// Shared plumbing for the LLM providers: single in-flight QNetworkReply with
// stale-callback guards, line-based streaming accumulation, and the
// finished-reply fallback ladder (streamed -> non-stream JSON body ->
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

protected:
    // Clears the streaming accumulators; call at the start of chatComplete.
    // Virtual: providers must override it to also clear their per-request
    // tool-call accumulators - otherwise a previous request's tool calls are
    // re-emitted with the next response on the same service instance.
    virtual void resetStreamState();

    // POSTs the body and wires readyRead/finished to the base plumbing.
    // Each request bumps m_requestGeneration; callbacks capture it and
    // short-circuit when superseded, so a stale reply can never touch the
    // live request's stream state.
    void startReply(QNetworkRequest& request, const QByteArray& body);

    // ── Protocol hooks ────────────────────────────────
    // One complete buffered line (no trailing newline). SSE services strip
    // their "data:" prefix here; NDJSON services parse the line directly.
    virtual void processLine(const QByteArray& line) = 0;
    // True when the accumulated stream state forms a complete answer.
    virtual bool streamComplete() const = 0;
    // Assemble the accumulated stream state into the final response.
    virtual LLMResponse streamResult() const = 0;
    // Short provider word for error formatting ("API" / "Anthropic" / "Ollama").
    virtual QString providerLabel() const = 0;
    // Error payload inside a JSON body; empty when the body is no error.
    virtual QString extractApiError(const QJsonObject& json) const = 0;
    // True when the JSON body is a full (non-streamed) response.
    virtual bool isCompleteBody(const QJsonObject& json) const = 0;
    // Parse a complete body (wraps the testable static parser).
    virtual LLMResponse parseBody(const QJsonObject& json) const = 0;

    // ── Accumulated streaming state ───────────────────
    QByteArray m_lineBuffer;  // trailing incomplete line
    QByteArray m_rawBody;     // full raw response (for error diagnostics)
    QString m_content;
    int m_tokensUsed{0};
    bool m_doneReceived{false};
    bool m_responseEmitted{false};
    bool m_errorEmitted{false};

    QString m_apiKey;
    QString m_model;
    QString m_baseUrl;

private:
    void onReadyRead();
    void handleReplyFinished(QNetworkReply* reply);
    void processIncoming();   // split m_lineBuffer into complete lines

    QNetworkAccessManager* m_nam;
    QNetworkReply* m_currentReply{nullptr};
    quint64 m_requestGeneration{0};   // bumped per request; stale callbacks exit
};

} // namespace NexusAI
