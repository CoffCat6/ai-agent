#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "core/utils/TokenEstimator.h"
#include "core/agent/LLMTypes.h"

namespace NexusAI {

class IMemory;
class IAIService;

// T8 (REQ-T8-01..05): episodic long-term memory service. Generates session
// summaries on task completion and retrieves relevant past summaries for
// injection into the system prompt of new sessions.
//
// Summary generation is asynchronous: the LLM call runs on a detached
// signal connection; failures are logged but never block the caller
// (REQ-T8-05). Retrieval is synchronous (FTS5 MATCH is fast) and also
// never throws — an empty array is returned on any error.
//
// Injection budget: retrieved summaries are capped at kMaxInjectionTokens
// (default 1500) via TokenEstimator (REQ-T8-03, R8).
class EpisodicService : public QObject {
    Q_OBJECT
public:
    // DFX: max tokens of episodic summaries injected into the system prompt.
    static constexpr int kMaxInjectionTokens = 1500;
    // DFX: max characters of a single generated summary.
    static constexpr int kMaxSummaryChars = 500;
    // Default top-k for FTS5 retrieval.
    static constexpr int kDefaultTopK = 3;

    explicit EpisodicService(QObject* parent = nullptr);

    void setMemory(IMemory* memory) { m_memory = memory; }
    void setLlmService(IAIService* service) { m_llmService = service; }

    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

    // REQ-T8-01: asynchronously generate a summary for the given session
    // and store it via IMemory::saveEpisodic. The LLM call is fire-and-forget;
    // the result is persisted when it arrives. Does nothing if disabled or
    // if the LLM service / memory is not configured.
    void generateAndStoreSummary(const QString& sessionId,
                                 const QJsonArray& messages);

    // REQ-T8-02: retrieve up to topK relevant summaries for the given query.
    // Returns a formatted string ready for system-prompt injection, or an
    // empty string if disabled / no results / error. The total token count
    // is capped at kMaxInjectionTokens (REQ-T8-03).
    QString retrieveRelevantSummaries(const QString& query,
                                      int topK = kDefaultTopK) const;

    // REQ-T8-02 (test hook): raw retrieval returning the FTS5 matches as a
    // JSON array (for assertions on hit count / content).
    QJsonArray retrieveRaw(const QString& query, int topK = kDefaultTopK) const;

signals:
    // Emitted when an async summary has been stored (or skipped on error).
    void summaryStored(const QString& sessionId, bool success);

private slots:
    void onSummaryResponse(const LLMResponse& response);

private:
    IMemory* m_memory{nullptr};
    IAIService* m_llmService{nullptr};
    bool m_enabled{false};

    // Pending summary request: maps LLM requestId → sessionId so the async
    // response can be routed to the correct saveEpisodic call.
    QHash<quint64, QString> m_pendingSummaries;
};

} // namespace NexusAI
