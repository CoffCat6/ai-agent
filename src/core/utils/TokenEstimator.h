#pragma once

#include <QString>
#include <QJsonArray>
#include <QJsonObject>
#include <QVariant>
#include <QVariantMap>
#include <QHash>
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
        int surrogatePairs = 0;
        for (int i = 0; i < text.size(); ++i) {
            const QChar ch = text.at(i);
            // A13: non-BMP characters (emoji, rare CJK) are a UTF-16
            // surrogate pair. Half of each pair is not a Latin character;
            // counting both units as "other / 4" under-estimated them by
            // ~4x. A full code point costs 2 tokens.
            if (ch.isHighSurrogate() && i + 1 < text.size()
                    && text.at(i + 1).isLowSurrogate()) {
                ++surrogatePairs;
                ++i;
                continue;
            }
            const char16_t u = ch.unicode();
            if ((u >= 0x2E80 && u <= 0x9FFF)     // CJK radicals .. unified ideographs
                || (u >= 0xAC00 && u <= 0xD7A3)  // A13: Hangul syllables
                || (u >= 0xF900 && u <= 0xFAFF)  // compatibility ideographs
                || (u >= 0x3000 && u <= 0x303F)  // CJK punctuation
                || (u >= 0xFF00 && u <= 0xFFEF)) // fullwidth forms
                ++cjk;
        }
        const int other = text.length() - cjk - surrogatePairs * 2;
        // ceil(other/4) + ceil(cjk/1.5), integer math; never zero for
        // non-empty text so compression budgets stay conservative.
        return qMax(1, (other + 3) / 4 + (cjk * 2 + 2) / 3 + surrogatePairs * 2);
    }

    // A13: truncate by UTF-16 index without splitting a surrogate pair.
    // When the cut would land between a high and a low surrogate, back off
    // one unit so the full code point stays intact (a lone surrogate is not
    // valid UTF-16 and corrupts JSON/tool payloads).
    static QString safeLeft(const QString& text, int keepChars) {
        if (keepChars <= 0) return QString();
        if (keepChars >= text.size()) return text;
        int cut = keepChars;
        if (text.at(cut).isLowSurrogate() && text.at(cut - 1).isHighSurrogate())
            --cut;
        return text.left(cut);
    }

    // Estimate tokens for a single message object
    static int estimateTokens(const QJsonObject& msg) {
        int total = 0;
        total += estimateTokens(msg.value("role").toString());
        total += estimateTokens(msg.value("content").toString());
        // Batch 30 (F6): attached images travel as vision payload. A flat
        // 1000 tokens per image is a deliberately conservative middle ground
        // (real cost varies with resolution); no resolution grading yet.
        const QJsonArray images = msg.value("images").toArray();
        total += images.size() * 1000;
        // Batch 30 (F6): text attachments fold into the request text.
        for (const auto& file : msg.value("files").toArray())
            total += estimateTokens(file.toObject().value("content").toString());
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
        for (const auto& t : tools)
            total += estimateToolTokens(t.toObject());
        return total;
    }

    // docs/16 §8.3（S1）: 单个工具定义的线上开销。抽成独立函数，让 TopBar 能把
    // 「工具定义」再拆成内置 / MCP（mcp__* 前缀）；总量口径与
    // estimateToolsSchema 逐字节一致，不会因为拆分而漂移。
    static int estimateToolTokens(const QJsonObject& tool) {
        return estimateTokens(QString::fromUtf8(
            QJsonDocument(tool).toJson(QJsonDocument::Compact)));
    }

    // docs/16 §8.3（S1）: 互斥的上下文构成明细（原始估算，未乘校准系数）。
    // 返回 key → tokens，约定：**所有行之和恒等于 estimateTokens(messages)**。
    //
    // 分类规则（顺序即优先级）：
    //   · role=system 且正文以压缩标记 "[Context compressed:" 开头 → compressed
    //     （compressedCount 单独给出标记条数）；其余 system → systemPrompt
    //   · role=tool → 按 tool_call_id 回查 assistant.tool_calls 的工具名，
    //     归入 tool.<category>；查不到（历史恢复/异常）→ tool.other
    //   · user / assistant → 正文分 user / assistant 两行；同一条消息里的
    //     tool_calls（name+arguments+8 的线上 framing）单独计入 toolCalls，
    //     附件正文计入 files，图片计入 images
    //
    // 工具结果分类只依赖 tool_call_id→name 映射，**不给消息添加任何字段**：
    // OpenAI Chat 路径会把整个 message 对象原样外发
    // （OpenAIService::convertMessagesForOpenAI 只删 images），多一个字段就是
    // 往请求体里塞垃圾，还会让估算与真实 prompt 彻底脱钩。
    static QVariantMap classifyMessages(const QJsonArray& messages) {
        // 第一趟：assistant.tool_calls 的 id → 工具名（第二趟查表归类）
        QHash<QString, QString> callNames;
        for (const auto& entry : messages) {
            const QJsonObject msg = entry.toObject();
            if (msg.value("role").toString() != QLatin1String("assistant")) continue;
            for (const auto& tc : msg.value("tool_calls").toArray()) {
                const QJsonObject call = tc.toObject();
                callNames.insert(call.value("id").toString(),
                                 call.value("function").toObject()
                                     .value("name").toString());
            }
        }

        qint64 systemPrompt = 0, compressed = 0, toolCalls = 0;
        qint64 files = 0, images = 0, user = 0, assistant = 0;
        // 工具结果 8 类，与 TopBar 二级图例一一对应
        qint64 toolFileRead = 0, toolCommand = 0, toolSearch = 0, toolGit = 0;
        qint64 toolSubagent = 0, toolKnowledge = 0, toolMcp = 0, toolOther = 0;
        int compressedCount = 0;

        for (const auto& entry : messages) {
            const QJsonObject msg = entry.toObject();
            const QString role = msg.value("role").toString();
            const int img = msg.value("images").toArray().size() * 1000;
            int fileTokens = 0;
            for (const auto& file : msg.value("files").toArray())
                fileTokens += estimateTokens(
                    file.toObject().value("content").toString());
            int callTokens = 0;
            for (const auto& tc : msg.value("tool_calls").toArray()) {
                const QJsonObject fn = tc.toObject().value("function").toObject();
                callTokens += 8
                            + estimateTokens(fn.value("name").toString())
                            + estimateTokens(fn.value("arguments").toString());
            }
            // 扣掉已单列的部分后，剩下的就是这个角色自己的开销
            // （role 串 + 正文 + 每消息 +4 framing），恒 >= 0。
            const int own = estimateTokens(msg) - img - fileTokens - callTokens;
            images += img;
            files += fileTokens;

            if (role == QLatin1String("system")) {
                const QString content = msg.value("content").toString();
                if (content.startsWith(QLatin1String("[Context compressed:"))) {
                    compressed += own;
                    ++compressedCount;
                } else {
                    systemPrompt += own;
                }
            } else if (role == QLatin1String("tool")) {
                const QString name =
                    callNames.value(msg.value("tool_call_id").toString());
                if (name.startsWith(QLatin1String("mcp__"))) toolMcp += own;
                else if (name == QLatin1String("read_file")
                         || name == QLatin1String("glob")
                         || name == QLatin1String("grep")
                         || name == QLatin1String("list_dir")) toolFileRead += own;
                else if (name == QLatin1String("shell_exec")) toolCommand += own;
                else if (name == QLatin1String("web_search")
                         || name == QLatin1String("http_fetch")) toolSearch += own;
                else if (name.startsWith(QLatin1String("git_"))) toolGit += own;
                else if (name == QLatin1String("spawn_agent")
                         || name.startsWith(QLatin1String("subagent_")))
                    toolSubagent += own;
                else if (name == QLatin1String("search_knowledge_base"))
                    toolKnowledge += own;
                else toolOther += own;
            } else {
                if (role == QLatin1String("user")) user += own;
                else assistant += own;
                toolCalls += callTokens;
            }
        }

        return QVariantMap{
            {QStringLiteral("systemPrompt"),      systemPrompt},
            {QStringLiteral("compressed"),        compressed},
            {QStringLiteral("compressedCount"),   compressedCount},
            {QStringLiteral("toolCalls"),         toolCalls},
            {QStringLiteral("files"),             files},
            {QStringLiteral("images"),            images},
            {QStringLiteral("user"),              user},
            {QStringLiteral("assistant"),         assistant},
            {QStringLiteral("tool.fileRead"),     toolFileRead},
            {QStringLiteral("tool.command"),      toolCommand},
            {QStringLiteral("tool.search"),       toolSearch},
            {QStringLiteral("tool.git"),          toolGit},
            {QStringLiteral("tool.subagent"),     toolSubagent},
            {QStringLiteral("tool.knowledge"),    toolKnowledge},
            {QStringLiteral("tool.mcp"),          toolMcp},
            {QStringLiteral("tool.other"),        toolOther},
        };
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
    // docs/16 §8.3（S3）: omittedMessages / truncated 是可选出参（默认 nullptr），
    // 让调用方能把"这次压缩掉了多少"报给 UI——此前只能靠 before/after 消息数
    // 猜，原地截断（消息数不变）完全不可见。默认参数保证既有调用点零改动。
    static QJsonArray compressToFit(const QJsonArray& messages, int maxTokens,
                                    int* omittedMessages = nullptr,
                                    bool* truncated = nullptr) {
        if (omittedMessages) *omittedMessages = 0;
        if (truncated) *truncated = false;
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
        if (omittedMessages && omitted > 0) *omittedMessages = omitted;
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

        // Best effort can still overflow: when the newest turn alone does not
        // fit (small window + large tools schema + a long reply), the array
        // above keeps it anyway and the request exceeds the window. Truncate
        // the longest ASSISTANT message (never the user's own words) so the
        // budget actually holds; the marker tells the model text was cut.
        // A6: an assistant tool-call turn has empty content, so historically
        // this loop found nothing and returned an over-budget array. Fall back
        // to the longest non-empty TOOL message — keep role/tool_call_id and
        // only cut content, so the assistant tool_calls pairing stays valid.
        const auto findLongest = [&out](const QString& role) {
            int worst = -1;
            int worstTokens = 0;
            for (int i = 1; i < out.size(); ++i) {
                const QJsonObject msg = out.at(i).toObject();
                if (msg.value("role").toString() != role) continue;
                if (msg.value("content").toString().isEmpty()) continue;
                const int tokens = estimateTokens(msg);
                if (tokens > worstTokens) {
                    worstTokens = tokens;
                    worst = i;
                }
            }
            return worst;
        };
        int total = estimateTokens(out);
        for (int guard = 0; guard < 8 && total > maxTokens; ++guard) {
            int worst = findLongest(QStringLiteral("assistant"));
            if (worst < 0) worst = findLongest(QStringLiteral("tool"));
            if (worst < 0) break;
            QJsonObject msg = out.at(worst).toObject();
            const QString content = msg.value("content").toString();
            // ~4 chars per token (Latin); CJK truncates even more aggressively,
            // which only makes the cut safer.
            const int excess = total - maxTokens;
            const int keepChars = qMax(0, content.size() - (excess * 4 + 64));
            if (keepChars == 0) break;
            msg["content"] = safeLeft(content, keepChars)
                + QStringLiteral("\n... [truncated to fit the context window]");
            out.replace(worst, msg);
            if (truncated) *truncated = true;
            const int next = estimateTokens(out);
            if (next >= total) break;   // no progress (framing overhead)
            total = next;
        }
        return out;
    }

    // docs/35 P4.1: sub-agent history compression. A sub-agent has exactly one
    // user message (its goal), so compressToFit() never finds a droppable
    // suffix there and silently leaves an overflowing history untouched. This
    // variant keeps the system+goal header and drops whole assistant→tool
    // rounds from the front — the only cut that preserves the OpenAI
    // tool_calls/tool pairing contract (a "tool" message is never kept without
    // the assistant turn that requested it). When even the newest round does
    // not fit, oversized tool results are halved in place and marked with a
    // truncation suffix. Best effort: if the header alone exceeds maxTokens,
    // the array is returned as-is.
    //
    // omittedRounds / truncatedResults (optional out-params) report how much
    // was cut, so the runner can surface a "context compressed" progress event
    // instead of the model silently losing earlier evidence.
    static QJsonArray compressToolRoundsToFit(const QJsonArray& messages,
                                              int maxTokens,
                                              int* omittedRounds = nullptr,
                                              int* truncatedResults = nullptr) {
        if (omittedRounds) *omittedRounds = 0;
        if (truncatedResults) *truncatedResults = 0;
        constexpr int kHeaderSize = 2;   // system prompt + goal
        if (messages.size() <= kHeaderSize) return messages;

        const auto roleAt = [&messages](int i) {
            return messages.at(i).toObject().value("role").toString();
        };

        // Group the history: an assistant turn plus the contiguous tool
        // results answering it travel together. Anything else stands alone.
        struct Group { int begin; int end; };
        QVector<Group> groups;
        for (int i = kHeaderSize; i < messages.size();) {
            int j = i + 1;
            if (roleAt(i) == QLatin1String("assistant")) {
                while (j < messages.size()
                       && roleAt(j) == QLatin1String("tool"))
                    ++j;
            }
            groups.append(Group{i, j});
            i = j;
        }
        if (groups.isEmpty()) return messages;

        const int headerTokens = estimateTokens(messages.at(0).toObject())
                               + estimateTokens(messages.at(1).toObject());
        QVector<int> suffix(groups.size() + 1, 0);
        for (int i = groups.size() - 1; i >= 0; --i) {
            for (int m = groups.at(i).begin; m < groups.at(i).end; ++m)
                suffix[i] += estimateTokens(messages.at(m).toObject());
            suffix[i] += suffix[i + 1];
        }

        const auto markerFor = [](int omitted) {
            QJsonObject marker;
            marker["role"] = "user";
            marker["content"] = QStringLiteral(
                "[Context compressed: %1 earlier tool round(s) were omitted "
                "to fit the model's context window. Continue the task from "
                "the remaining history.]").arg(omitted);
            return marker;
        };

        // Smallest group index whose (possibly marked) suffix fits.
        int keep = -1;
        for (int i = 0; i < groups.size(); ++i) {
            const int markerTokens = i > 0
                ? estimateTokens(markerFor(i)) : 0;
            if (headerTokens + markerTokens + suffix.at(i) <= maxTokens) {
                keep = i;
                break;
            }
        }
        // Best effort: keep the newest round and shrink its tool results.
        if (keep < 0) keep = groups.size() - 1;

        QJsonArray out;
        out.append(messages.at(0));
        out.append(messages.at(1));
        if (keep > 0) out.append(markerFor(keep));
        for (int i = keep; i < groups.size(); ++i) {
            for (int m = groups.at(i).begin; m < groups.at(i).end; ++m)
                out.append(messages.at(m));
        }
        if (omittedRounds) *omittedRounds = keep;

        // Truncation fallback: still over budget (tiny window or one huge
        // result). Halve the largest kept tool result until it fits; the
        // strict-decrease guard prevents an infinite loop once the suffix
        // text no longer leaves room to cut.
        constexpr int kMinToolContentChars = 400;
        const QString truncSuffix = QStringLiteral(
            "\n... [truncated to fit context]");
        int truncated = 0;
        while (estimateTokens(out) > maxTokens) {
            int largest = -1;
            int largestLen = 0;
            for (int i = kHeaderSize; i < out.size(); ++i) {
                const QJsonObject msg = out.at(i).toObject();
                if (msg.value("role").toString() != QLatin1String("tool"))
                    continue;
                const int len = msg.value("content").toString().length();
                if (len > largestLen) {
                    largest = i;
                    largestLen = len;
                }
            }
            if (largest < 0) break;
            const int newLen = qMax(kMinToolContentChars, largestLen / 2);
            if (newLen + truncSuffix.length() >= largestLen) break;
            QJsonObject msg = out.at(largest).toObject();
            msg["content"] = safeLeft(msg.value("content").toString(), newLen)
                             + truncSuffix;
            out.replace(largest, msg);
            ++truncated;
        }
        if (truncatedResults) *truncatedResults = truncated;
        return out;
    }

    // 上下文构成拆分（TopBar 容量占比浮层，docs/16 §8.3）：把当前消息数组
    // 按 role 拆成互斥的 token 桶。system → systemPrompt；user/assistant →
    // conversation；tool → toolResults；图片负载从宿主消息中单列（避免与
    // conversation 重复计入）。`reservedTokens` 是下一次请求的 tools schema
    // 预留——不属于消息数组，单独成桶。约定：四桶之和 == estimateTokens(
    // messages)（即现有 usedTokens 口径），加 reserved 即窗口真实占用。
    static QVariantMap splitContextTokens(const QJsonArray& messages,
                                          int reservedTokens) {
        qint64 systemPrompt = 0, conversation = 0, toolResults = 0, images = 0;
        for (const auto& entry : messages) {
            const QJsonObject msg = entry.toObject();
            const int img = msg.value("images").toArray().size() * 1000;
            images += img;
            const int est = estimateTokens(msg);
            const QString role = msg.value("role").toString();
            if (role == QLatin1String("system"))
                systemPrompt += est;
            else if (role == QLatin1String("tool"))
                toolResults += est;
            else
                conversation += est - img;
        }
        const int used = estimateTokens(messages);
        return QVariantMap{
            {QStringLiteral("systemPrompt"), systemPrompt},
            {QStringLiteral("conversation"), conversation},
            {QStringLiteral("toolResults"),  toolResults},
            {QStringLiteral("images"),       images},
            {QStringLiteral("toolsSchema"),  reservedTokens},
            {QStringLiteral("usedTokens"),   used},
            {QStringLiteral("totalTokens"),  used + reservedTokens},
        };
    }
};

} // namespace NexusAI
