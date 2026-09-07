#pragma once

#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

namespace NexusAI {

// Simple token estimator for context window management
class TokenEstimator {
public:
    // Rough estimate: ~4 characters per token for Latin text; CJK characters
    // cost roughly 1.5 characters per token (most Chinese chars are ~0.6-0.7
    // tokens). The previous uniform /4 rule underestimated Chinese
    // conversations by about 2x and let them overflow the real context
    // window before compression kicked in.
    static int estimateTokens(const QString& text) {
        if (text.isEmpty()) return 0;
        int cjk = 0;
        for (const QChar ch : text) {
            const char16_t u = ch.unicode();
            if ((u >= 0x2E80 && u <= 0x9FFF)     // CJK radicals .. unified ideographs
                || (u >= 0xF900 && u <= 0xFAFF)  // compatibility ideographs
                || (u >= 0x3000 && u <= 0x303F)  // CJK punctuation
                || (u >= 0xFF00 && u <= 0xFFEF)) // fullwidth forms
                ++cjk;
        }
        const int other = text.length() - cjk;
        // ceil(other/4) + ceil(cjk/1.5), integer math; never zero for
        // non-empty text so compression budgets stay conservative.
        return qMax(1, (other + 3) / 4 + (cjk * 2 + 2) / 3);
    }

    // Estimate tokens for a single message object
    static int estimateTokens(const QJsonObject& msg) {
        int total = 0;
        total += estimateTokens(msg.value("role").toString());
        total += estimateTokens(msg.value("content").toString());
        // Overhead per message (~4 tokens for formatting)
        total += 4;
        // REL-1002: assistant tool_calls carry a function name plus JSON
        // arguments on the wire — previously uncounted, so tool-heavy
        // conversations under-estimated their real context size.
        for (const auto& tc : msg.value("tool_calls").toArray()) {
            const QJsonObject fn = tc.toObject().value("function").toObject();
            total += 8;   // per-call wire framing (id/type/keys)
            total += estimateTokens(fn.value("name").toString());
            total += estimateTokens(fn.value("arguments").toString());
        }
        return total;
    }

    // REL-1002: the tools schema travels with EVERY request — the budget
    // must reserve room for it before compressing history.
    static int estimateToolsSchema(const QJsonArray& tools) {
        int total = 0;
        for (const auto& t : tools) {
            const QJsonDocument doc(t.toObject());
            total += estimateTokens(
                QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
        }
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
