#pragma once

#include <QString>
#include <QJsonArray>
#include <QJsonObject>

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
};

} // namespace NexusAI
