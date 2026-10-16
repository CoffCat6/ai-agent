#pragma once

#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <functional>

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
//
// Embedding generation is also asynchronous: the episode is saved first
// (without embedding), then a worker thread generates the embedding and
// updates the row via updateEpisodicEmbedding on the main thread.
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
    void setEmbedder(std::function<QVector<float>(const QString&)> embedder) { m_embedder = std::move(embedder); }

    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

    // docs/24 H4 (test hook): the pending-request deadline. A superseded
    // request never emits a response, so the entry would otherwise leak.
    void setSummaryTimeoutMs(int ms) { m_summaryTimeoutMs = ms; }

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
    //
    // Synchronous path: blocks on the embedder round-trip when one is set —
    // kept as the test hook / non-GUI callers. GUI callers (AIAgent) use
    // retrieveRelevantSummariesAsync (docs/25 B3) instead.
    QString retrieveRelevantSummaries(const QString& query,
                                      int topK = kDefaultTopK) const;

    // REQ-T8-02 (test hook): raw retrieval returning the FTS5 matches as a
    // JSON array (for assertions on hit count / content). Episodes carry a
    // unified "score" field (docs/25 B2: FTS hits 1.0, semantic hits their
    // cosine) and the merged set is topK-capped.
    QJsonArray retrieveRaw(const QString& query, int topK = kDefaultTopK) const;

    // docs/25 B3: async retrieval. The embedder network round-trip runs on
    // a QtConcurrent worker; the DB scans (QSqlDatabase connections are
    // thread-affine) and the in-memory cosine merge run on this service's
    // thread. The injection text is delivered via retrievalReady. Returns
    // the request id (0 = request not applicable; no signal will fire).
    quint64 retrieveRelevantSummariesAsync(const QString& query,
                                           int topK = kDefaultTopK);

signals:
    // Emitted when an async summary has been stored (or skipped on error).
    void summaryStored(const QString& sessionId, bool success);
    // docs/25 B3: async retrieval result. Empty injection = nothing found
    // (or the retrieval failed) — callers treat it as "skip injection".
    void retrievalReady(quint64 requestId, const QString& injection);

private slots:
    // Called on the main thread (via QueuedConnection) after a worker thread
    // finishes generating the embedding BLOB.
    void applyEmbedding(qint64 id, const QByteArray& blob);

private:
    // docs/24 H4: routed by the monotonic request id captured in the
    // one-shot connection — a stale/unknown response is dropped instead of
    // stealing the first pending entry (the old FIFO pairing misrouted
    // concurrent summaries).
    void onSummaryResponse(quint64 requestId, const LLMResponse& response);

    // docs/25 B3: service-thread completion of an async retrieval (queued
    // back from the worker): merge FTS + semantic, build the injection,
    // emit retrievalReady.
    void completeRetrieval(quint64 requestId, const QString& query,
                           const QVector<float>& queryVec, int topK);

    // docs/25 B2/B3: shared retrieval core — FTS hits merged with semantic
    // hits by unified score, topK-capped. queryVec empty = FTS only.
    QJsonArray retrieveMerged(const QString& query,
                              const QVector<float>& queryVec, int topK) const;
    // Format retrieved episodes into the capped injection text.
    QString buildInjection(const QJsonArray& results) const;

    IMemory* m_memory{nullptr};
    IAIService* m_llmService{nullptr};
    std::function<QVector<float>(const QString&)> m_embedder;
    bool m_enabled{false};

    // Pending summary request: maps LLM requestId → sessionId so the async
    // response can be routed to the correct saveEpisodic call. At most one
    // entry exists at a time: the LLM service keeps a single in-flight
    // request, and a newer chatComplete silently supersedes the older one.
    QHash<quint64, QString> m_pendingSummaries;
    quint64 m_nextSummaryRequestId{1};
    int m_summaryTimeoutMs{90000};

    // docs/25 B3: monotonic id for async retrievals (stale results are
    // dropped by the receiver's one-shot connection).
    quint64 m_nextRetrievalRequestId{1};
};

} // namespace NexusAI
