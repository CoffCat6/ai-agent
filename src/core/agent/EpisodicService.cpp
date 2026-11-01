#include "EpisodicService.h"

#include "core/agent/LLMTypes.h"
#include "core/memory/IMemory.h"
#include "core/utils/Activity.h"
#include "core/utils/Logger.h"
#include "infrastructure/llm/IAIService.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPointer>
#include <QSet>
#include <QtConcurrent>
#include <QTimer>
#include <algorithm>

Q_LOGGING_CATEGORY(lcEpisodic, "nexus.episodic")

namespace NexusAI {

namespace {

// docs/25 B2: unified-score merge of FTS and semantic retrieval results.
// FTS hits score 1.0 (an explicit keyword match outranks any cosine
// similarity); semantic hits carry their cosine "score" from
// searchEpisodicSemantic. Deduplicated by episode id, sorted by score
// descending, topK kept — the old append-after ordering let weak semantic
// hits ride along behind the FTS block regardless of relevance.
QJsonArray mergeRetrieval(const QJsonArray& fts, const QJsonArray& semantic,
                          int topK) {
    struct Item { double score; QJsonObject episode; };
    QVector<Item> items;
    QSet<qint64> seen;
    const auto push = [&](QJsonObject episode, double score) {
        const qint64 id = episode.value("id").toVariant().toLongLong();
        if (seen.contains(id)) return;
        seen.insert(id);
        episode["score"] = score;
        items.append({score, episode});
    };
    for (const auto& v : fts)
        push(v.toObject(), 1.0);
    for (const auto& v : semantic)
        push(v.toObject(), v.toObject().value("score").toDouble(0.0));
    std::sort(items.begin(), items.end(),
              [](const Item& a, const Item& b) { return a.score > b.score; });
    QJsonArray out;
    const int n = qMin(topK, items.size());
    for (int i = 0; i < n; ++i)
        out.append(items[i].episode);
    return out;
}

} // namespace

EpisodicService::EpisodicService(QObject* parent)
    : QObject(parent) {}

void EpisodicService::generateAndStoreSummary(const QString& sessionId,
                                               const QJsonArray& messages) {
    // docs/26 I4: 会话标题生成串行在本服务之后、以 summaryStored 为触发
    // 信号——因此"不走摘要"的早退也必须发（成败皆可的语义是「流程结束」
    // 而非「摘要成功」），否则禁用/无服务时标题链永久挂起。
    if (!m_enabled || !m_memory || !m_llmService || sessionId.isEmpty()) {
        emit summaryStored(sessionId, false);
        return;
    }

    // Build a compact conversation transcript for the summarizer.
    // Skip the system message (index 0) and keep only user/assistant turns.
    QStringList turns;
    for (const auto& msgVal : messages) {
        const QJsonObject msg = msgVal.toObject();
        const QString role = msg.value(QStringLiteral("role")).toString();
        if (role == QLatin1String("system")) continue;
        const QString content = msg.value(QStringLiteral("content")).toString();
        if (content.isEmpty()) continue;
        turns.append(QStringLiteral("%1: %2").arg(role == QLatin1String("user")
            ? QStringLiteral("用户") : QStringLiteral("助手"), content));
    }
    if (turns.isEmpty()) {
        emit summaryStored(sessionId, false);
        return;
    }

    // Cap the transcript to avoid excessive prompt size.
    const QString transcript = turns.join(QStringLiteral("\n")).left(4000);

    const QString summaryPrompt = QStringLiteral(
        "请用中文将以下对话总结为一段不超过 %1 个字符的摘要，"
        "提炼关键话题、用户意图和结论。只输出摘要文本，不要加前缀。\n\n%2"
    ).arg(kMaxSummaryChars).arg(transcript);

    const QJsonArray requestMessages = QJsonArray{
        QJsonObject{{"role", "system"},
                    {"content", "你是一个对话摘要助手。"}},
        QJsonObject{{"role", "user"},
                    {"content", summaryPrompt}}
    };

    // docs/24 H4: the LLM service keeps ONE in-flight request — a second
    // chatComplete silently supersedes the older one (no signal). Queueing
    // several pending summaries would misattribute the single response
    // (the old FIFO pairing stored it under the wrong session), so the
    // newest request wins and the stale one is reported as dropped.
    if (!m_pendingSummaries.isEmpty()) {
        const QString staleSession =
            m_pendingSummaries.take(m_pendingSummaries.keys().first());
        qCWarning(lcEpisodic, "Summary request superseded; dropped session %s",
                  qPrintable(staleSession));
        emit summaryStored(staleSession, false);
    }

    // Track the pending request so the async response can be routed.
    const quint64 requestId = m_nextSummaryRequestId++;
    m_pendingSummaries.insert(requestId, sessionId);

    // One-shot connection: disconnect after the first response. The lambda
    // captures its own request id — a broadcast response that reaches a
    // stale connection routes to nothing instead of stealing an entry.
    const auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_llmService, &IAIService::responseReady,
                    this, [this, conn, requestId](const LLMResponse& response) {
        disconnect(*conn);
        onSummaryResponse(requestId, response);
    });

    // Deadline: a superseded request never emits responseReady — without
    // this both the one-shot connection and the pending entry leak forever.
    QTimer::singleShot(m_summaryTimeoutMs, this,
                       [this, conn, requestId, sessionId]() {
        if (m_pendingSummaries.remove(requestId) > 0) {
            disconnect(*conn);
            qCWarning(lcEpisodic, "Summary request timed out for session %s",
                      qPrintable(sessionId));
            emit summaryStored(sessionId, false);
        }
    });

    Activity::usage(Activity::Kind::Agent, QStringLiteral("episodic.summary"),
                    sessionId, 0, QStringLiteral("generate"));

    m_llmService->chatComplete(requestMessages);
}

void EpisodicService::onSummaryResponse(quint64 requestId,
                                        const LLMResponse& response) {
    // docs/24 H4: strict pairing — anything not matching the live request
    // (stale connection, late reply after timeout) is dropped.
    if (!m_pendingSummaries.contains(requestId)) return;
    const QString sessionId = m_pendingSummaries.take(requestId);

    if (!response.success || response.content.isEmpty()) {
        qCWarning(lcEpisodic, "Summary generation failed for session %s: %s",
                  qPrintable(sessionId), qPrintable(response.error));
        emit summaryStored(sessionId, false);
        return;
    }

    const QString summary = response.content.trimmed().left(kMaxSummaryChars);
    if (summary.isEmpty()) {
        emit summaryStored(sessionId, false);
        return;
    }

    // Extract simple tags from the summary (first few words).
    const QStringList words = summary.split(QStringLiteral(" "),
        Qt::SkipEmptyParts);
    const QStringList tags = words.mid(0, qMin(5, words.size()));
    const QString tagStr = tags.join(QStringLiteral(","));

    QJsonObject episode;
    episode["summary"] = summary;
    episode["tags"] = tagStr;

    // Save episode first (without embedding), then generate embedding async.
    const qint64 episodeId = m_memory->saveEpisodic(sessionId, episode);

    Activity::usage(Activity::Kind::Agent, QStringLiteral("episodic.summary"),
                    sessionId, 0, QStringLiteral("stored len=%1").arg(summary.size()));

    emit summaryStored(sessionId, true);

    // Asynchronously generate embedding on a worker thread, then update
    // the row on the main thread (SQLite is not thread-safe).
    if (m_embedder && episodeId > 0) {
        const auto embedder = m_embedder;
        QPointer<EpisodicService> guard(this);
        QtConcurrent::run([guard, episodeId, summary, embedder]() {
            const QVector<float> emb = embedder(summary);
            if (emb.isEmpty() || guard.isNull()) return;
            const QByteArray blob(reinterpret_cast<const char*>(emb.constData()),
                                  int(emb.size() * sizeof(float)));
            QMetaObject::invokeMethod(guard.data(), "applyEmbedding",
                                      Qt::QueuedConnection,
                                      Q_ARG(qint64, episodeId),
                                      Q_ARG(QByteArray, blob));
        });
    }
}

QString EpisodicService::retrieveRelevantSummaries(const QString& query,
                                                     int topK) const {
    if (!m_enabled || !m_memory || query.trimmed().isEmpty())
        return {};
    return buildInjection(retrieveRaw(query, topK));
}

QString EpisodicService::buildInjection(const QJsonArray& results) const {
    if (results.isEmpty()) return {};

    // Build the injection text, capping at kMaxInjectionTokens.
    QStringList lines;
    lines.append(QStringLiteral("\n\n## 历史会话摘要（episodic memory）"));
    int usedTokens = TokenEstimator::estimateTokens(lines.first());

    for (const auto& val : results) {
        const QJsonObject ep = val.toObject();
        const QString summary = ep.value(QStringLiteral("summary")).toString();
        if (summary.isEmpty()) continue;

        const QString entry = QStringLiteral("- %1").arg(summary);
        const int entryTokens = TokenEstimator::estimateTokens(entry);

        if (usedTokens + entryTokens > kMaxInjectionTokens) break;

        lines.append(entry);
        usedTokens += entryTokens;
    }

    if (lines.size() <= 1) return {};  // only the header, no content

    return lines.join(QStringLiteral("\n"));
}

QJsonArray EpisodicService::retrieveRaw(const QString& query, int topK) const {
    if (!m_enabled || !m_memory || query.trimmed().isEmpty())
        return {};

    // docs/25 B2: the embedder call stays on the synchronous path only —
    // GUI callers must use retrieveRelevantSummariesAsync (B3).
    const QVector<float> queryVec =
        m_embedder ? m_embedder(query) : QVector<float>{};
    return retrieveMerged(query, queryVec, topK);
}

QJsonArray EpisodicService::retrieveMerged(const QString& query,
                                           const QVector<float>& queryVec,
                                           int topK) const {
    QJsonArray fts = m_memory->searchEpisodic(query, topK);
    if (queryVec.isEmpty())
        return fts;   // no embedder / embed failure: FTS results only
    return mergeRetrieval(fts, m_memory->searchEpisodicSemantic(queryVec, topK),
                          topK);
}

quint64 EpisodicService::retrieveRelevantSummariesAsync(const QString& query,
                                                        int topK) {
    if (!m_enabled || !m_memory || query.trimmed().isEmpty())
        return 0;

    const quint64 requestId = m_nextRetrievalRequestId++;

    if (!m_embedder) {
        // FTS-only retrieval is a local index scan: complete inline so the
        // caller receives the result with no worker hop.
        emit retrievalReady(requestId, buildInjection(retrieveMerged(query, {}, topK)));
        return requestId;
    }

    // docs/25 B3: the embedding network round-trip runs on a worker thread
    // (same pattern as the summary embedding in onSummaryResponse —
    // OllamaEmbedding::embed is worker-safe via its stack-local NAM). The
    // DB scans and the in-memory cosine stay on this service's thread: a
    // QSqlDatabase connection must be used from the thread that opened it.
    const auto embedder = m_embedder;
    QPointer<EpisodicService> guard(this);
    QtConcurrent::run([guard, requestId, query, topK, embedder]() {
        const QVector<float> queryVec = embedder(query);
        if (guard.isNull()) return;
        QMetaObject::invokeMethod(guard.data(),
                                  [guard, requestId, query, queryVec, topK]() {
            // Queued: runs on the service thread; dropped automatically if
            // the service was destroyed in between.
            guard->completeRetrieval(requestId, query, queryVec, topK);
        }, Qt::QueuedConnection);
    });
    return requestId;
}

void EpisodicService::completeRetrieval(quint64 requestId, const QString& query,
                                        const QVector<float>& queryVec, int topK) {
    if (!m_memory) {
        emit retrievalReady(requestId, QString());
        return;
    }
    emit retrievalReady(requestId, buildInjection(retrieveMerged(query, queryVec, topK)));
}

void EpisodicService::applyEmbedding(qint64 id, const QByteArray& blob) {
    if (m_memory)
        m_memory->updateEpisodicEmbedding(id, blob);
}

} // namespace NexusAI
