#pragma once

#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

namespace NexusAI {

// Simple token estimator for context window management
class TokenEstimator {
public:
    // Rough estimate: ~4 characters per token (English text average)
    static int estimateTokens(const QString& text) {
        if (text.isEmpty()) return 0;
        return text.length() / 4;
    }

    // Estimate tokens for a single message object
    static int estimateTokens(const QJsonObject& msg) {
        int total = 0;
        total += estimateTokens(msg.value("role").toString());
        total += estimateTokens(msg.value("content").toString());
        // Overhead per message (~4 tokens for formatting)
        total += 4;
        return total;
    }

    // Estimate total tokens for a message array
    static int estimateTokens(const QJsonArray& messages) {
        int total = 0;
        for (const auto& msg : messages) {
            total += estimateTokens(msg.toObject());
        }
        return total;
    }

    // Check if messages exceed token budget
    static bool exceedsBudget(const QJsonArray& messages, int maxTokens) {
        return estimateTokens(messages) > maxTokens;
    }

    // Calculate how many tokens can still be added
    static int remainingBudget(const QJsonArray& messages, int maxTokens) {
        return qMax(0, maxTokens - estimateTokens(messages));
    }

    // Compress a message history to fit maxTokens: keeps the system prompt
    // (first message), inserts an omission marker, and keeps the largest
    // contiguous suffix that starts at a user message. Starting the kept
    // range at a user message guarantees no orphan "tool" result messages
    // (which would break the OpenAI tool_call/tool pairing contract).
    static QJsonArray compressToFit(const QJsonArray& messages, int maxTokens) {
        if (messages.isEmpty()) return messages;

        // Latest user message that may start the kept suffix
        int lastUserIdx = -1;
        for (int i = messages.size() - 1; i >= 1; --i) {
            if (messages.at(i).toObject().value("role").toString() == "user") {
                lastUserIdx = i;
                break;
            }
        }
        if (lastUserIdx < 0) return messages; // nothing safely droppable

        // suffix[i] = estimated tokens of messages[i..end)
        QVector<int> suffix(messages.size() + 1, 0);
        for (int i = messages.size() - 1; i >= 0; --i)
            suffix[i] = suffix[i + 1] + estimateTokens(messages.at(i).toObject());

        constexpr int kMarkerReserve = 24; // rough budget for the marker text

        // Smallest start index (largest suffix) beginning at a user message
        // that fits alongside the system prompt and the marker.
        int keepFrom = -1;
        for (int i = 1; i <= lastUserIdx; ++i) {
            if (messages.at(i).toObject().value("role").toString() != "user") continue;
            if (suffix[i] + kMarkerReserve
                    + estimateTokens(messages.first().toObject()) <= maxTokens) {
                keepFrom = i;
                break;
            }
        }
        if (keepFrom < 0) keepFrom = lastUserIdx; // best effort: keep latest turn

        const int omitted = keepFrom - 1;
        QJsonArray out;
        out.append(messages.first());
        if (omitted > 0) {
            QJsonObject marker;
            marker["role"] = "system";
            marker["content"] = QStringLiteral(
                "[Context compressed: %1 earlier messages were omitted to fit "
                "the model's context window.]").arg(omitted);
            out.append(marker);
        }
        for (int i = keepFrom; i < messages.size(); ++i)
            out.append(messages.at(i));
        return out;
    }
};

} // namespace NexusAI
