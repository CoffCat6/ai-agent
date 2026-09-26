#include "EpisodicService.h"

#include "core/agent/LLMTypes.h"
#include "core/memory/IMemory.h"
#include "core/utils/Activity.h"
#include "core/utils/Logger.h"
#include "infrastructure/llm/IAIService.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcEpisodic, "nexus.episodic")

namespace NexusAI {

EpisodicService::EpisodicService(QObject* parent)
    : QObject(parent) {}

void EpisodicService::generateAndStoreSummary(const QString& sessionId,
                                               const QJsonArray& messages) {
    if (!m_enabled || !m_memory || !m_llmService || sessionId.isEmpty())
        return;

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
    if (turns.isEmpty()) return;

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

    // Track the pending request so the async response can be routed.
    // We use a signal connection that self-resolves on responseReady.
    const auto requestId = m_pendingSummaries.size() + 1;  // simple monotonic
    m_pendingSummaries.insert(requestId, sessionId);

    // One-shot connection: disconnect after the first response.
    const auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_llmService, &IAIService::responseReady,
                    this, [this, conn](const LLMResponse& response) {
        disconnect(*conn);
        onSummaryResponse(response);
    });

    Activity::usage(Activity::Kind::Agent, QStringLiteral("episodic.summary"),
                    sessionId, 0, QStringLiteral("generate"));

    m_llmService->chatComplete(requestMessages);
}

void EpisodicService::onSummaryResponse(const LLMResponse& response) {
    // Find the sessionId for this response. Since we use one-shot connections,
    // there should be exactly one pending entry.
    if (m_pendingSummaries.isEmpty()) return;

    // Take the first pending session (FIFO is fine for this async pattern).
    const QString sessionId = m_pendingSummaries.take(m_pendingSummaries.keys().first());

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

    m_memory->saveEpisodic(sessionId, episode);

    Activity::usage(Activity::Kind::Agent, QStringLiteral("episodic.summary"),
                    sessionId, 0, QStringLiteral("stored len=%1").arg(summary.size()));

    emit summaryStored(sessionId, true);
}

QString EpisodicService::retrieveRelevantSummaries(const QString& query,
                                                    int topK) const {
    if (!m_enabled || !m_memory || query.trimmed().isEmpty())
        return {};

    const QJsonArray results = retrieveRaw(query, topK);
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

    return m_memory->searchEpisodic(query, topK);
}

} // namespace NexusAI
