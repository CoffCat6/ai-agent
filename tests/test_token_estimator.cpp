// TokenEstimator: token accounting and history compression invariants.
#include "core/utils/TokenEstimator.h"

#include <QtTest>

using namespace NexusAI;

namespace {

QJsonObject msg(const QString& role, const QString& content) {
    QJsonObject m;
    m["role"] = role;
    m["content"] = content;
    return m;
}

bool anyContentContains(const QJsonArray& messages, const QString& needle) {
    for (const auto& entry : messages) {
        if (entry.toObject().value("content").toString().contains(needle))
            return true;
    }
    return false;
}

// A13: UTF-16 代理对（emoji 等非 BMP 字符）必须保持完整；孤立代理字符
// 会让 QString/JSON 序列化损坏。
bool hasLoneSurrogate(const QString& text) {
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch.isHighSurrogate()) {
            if (i + 1 >= text.size() || !text.at(i + 1).isLowSurrogate())
                return true;
            ++i;
        } else if (ch.isLowSurrogate()) {
            return true;
        }
    }
    return false;
}

// docs/35 P4.1: a sub-agent's wire history — system prompt + one goal, then
// assistant/tool rounds (the shape compressToolRoundsToFit must handle; the
// generic compressToFit only understands user-anchored conversations).
QJsonArray buildSubAgentHistory(int rounds, int toolChars) {
    QJsonArray history;
    history.append(msg("system", QString(200, 's')));
    history.append(msg("user", QStringLiteral("investigate the project")));
    for (int i = 0; i < rounds; ++i) {
        QJsonObject assistant;
        assistant["role"] = "assistant";
        assistant["content"] = QJsonValue();
        QJsonArray calls;
        QJsonObject call;
        call["id"] = QStringLiteral("call_%1").arg(i);
        call["type"] = "function";
        QJsonObject fn;
        fn["name"] = "read_file";
        fn["arguments"] = QStringLiteral("{\"path\":\"file%1\"}").arg(i);
        call["function"] = fn;
        calls.append(call);
        assistant["tool_calls"] = calls;
        history.append(assistant);
        QJsonObject toolMsg =
            msg("tool", QStringLiteral("round %1 ").arg(i)
                            + QString(toolChars, 't'));
        toolMsg["tool_call_id"] = QStringLiteral("call_%1").arg(i);
        history.append(toolMsg);
    }
    return history;
}

QJsonArray buildOversizedHistory() {
    QJsonArray history;
    history.append(msg("system", QString(200, 's')));
    for (int i = 0; i < 40; ++i) {
        history.append(msg("user", QStringLiteral("question %1 ").arg(i) + QString(300, 'q')));
        history.append(msg("assistant", QString(200, 'a'))); // tool-call turn
        history.append(msg("tool", QString(400, 't')));
        history.append(msg("assistant", QString(150, 'r')));
    }
    return history;
}

} // namespace

class TestTokenEstimator : public QObject {
    Q_OBJECT

private slots:
    // Batch 30 (F6): attached images count a flat 1000 tokens each.
    void imagesCostFlatPerImage() {
        const QJsonObject plain{
            {"role", "user"}, {"content", "hello world"}};
        const int withoutImages = TokenEstimator::estimateTokens(plain);

        QJsonObject withImages = plain;
        withImages["images"] = QJsonArray{
            QStringLiteral("data:image/jpeg;base64,QUJD"),
            QStringLiteral("data:image/jpeg;base64,REVG")};
        const int withTwo = TokenEstimator::estimateTokens(withImages);

        QCOMPARE(withTwo - withoutImages, 2 * 1000);
    }

    void estimateBasics();
    void estimateCjkWeighted();
    // A13: Hangul 音节也是 CJK 成本；非 BMP 字符（代理对）按 2 token。
    void estimateHangulWeighted();
    void estimateEmojiSurrogatePairs();
    // A13: 截断不得劈开代理对（压缩后的 tool 正文无孤立代理字符）。
    void compressDoesNotSplitSurrogatePair();
    void compressFittingHistoryUnchanged();
    void compressKeepsSystemAndSuffix();
    void compressSuffixStartsAtUser();
    void compressFitsBudget();
    void compressInsertsMarker();
    void compressGiantLastTurnBestEffort();
    // docs/16 §8.3（S3）: 压缩出参（省略条数 / 是否原地截断），供 UI 显示
    // "已压缩 N 条"。默认参数路径必须保持旧行为。
    void compressReportsOmittedAndTruncated();
    // A6: assistant(tool_calls, content 空) 时兜底截断本轮超大 tool 结果
    void compressTruncatesCurrentToolResult();
    void compressNoUserAnchorUnchanged();
    // docs/35 P4.1: sub-agent compression drops whole assistant→tool rounds
    // from the front (never a lone tool message) and truncates oversized
    // results when dropping rounds is not enough.
    void compressToolRoundsDropsOldestKeepsPairs();
    void compressToolRoundsTruncatesHugeResult();
    void compressToolRoundsFittingHistoryUnchanged();
    // Batch 10 (REL-1002): token accounting must include assistant
    // tool_calls arguments and the tools schema sent with each request.
    void estimateCountsToolCallsAndSchema() {
        QJsonObject plain;
        plain["role"] = QStringLiteral("assistant");
        plain["content"] = QStringLiteral("hello");
        const int plainTokens = TokenEstimator::estimateTokens(plain);

        QJsonObject withCalls;
        withCalls["role"] = QStringLiteral("assistant");
        withCalls["content"] = QJsonValue();   // null when only tool calls
        QJsonArray calls;
        QJsonObject call;
        call["id"] = QStringLiteral("c1");
        call["type"] = QStringLiteral("function");
        QJsonObject fn;
        fn["name"] = QStringLiteral("write_file");
        fn["arguments"] = QString(800, u'a');   // long JSON arguments
        call["function"] = fn;
        calls.append(call);
        withCalls["tool_calls"] = calls;
        const int callTokens = TokenEstimator::estimateTokens(withCalls);

        QVERIFY2(callTokens > plainTokens + 100,
                 "tool_calls arguments are not counted");

        QJsonArray tools;
        QJsonObject tool;
        tool["type"] = QStringLiteral("function");
        QJsonObject tfn;
        tfn["name"] = QStringLiteral("search_knowledge_base");
        tfn["description"] = QString(600, u'd');
        tfn["parameters"] = QJsonObject{{"type", QStringLiteral("object")}};
        tool["function"] = tfn;
        tools.append(tool);
        QVERIFY2(TokenEstimator::estimateToolsSchema(tools) > 100,
                 "tools schema is not counted");
    }

    // docs/16 §8.3: splitContextTokens buckets the live context by role.
    // Invariants: buckets are exclusive, sum == estimateTokens(messages),
    // and image payloads are broken out of their host message.
    void splitContextTokensBuckets() {
        QJsonObject sys = msg("system", QString(400, u's'));
        QJsonObject user = msg("user", QString(200, u'u'));
        QJsonArray userImgs;
        userImgs.append(QStringLiteral("data:image/jpeg;base64,QUJD"));
        user["images"] = userImgs;   // host message carries an image payload
        QJsonObject assistant = msg("assistant", QString(300, u'a'));
        QJsonObject tool = msg("tool", QString(500, u't'));

        QJsonArray messages;
        messages.append(sys);
        messages.append(user);
        messages.append(assistant);
        messages.append(tool);

        const QVariantMap split =
            TokenEstimator::splitContextTokens(messages, 777);
        QVERIFY(split.value("systemPrompt").toLongLong() > 0);
        QVERIFY(split.value("conversation").toLongLong() > 0);
        QVERIFY(split.value("toolResults").toLongLong() > 0);
        QCOMPARE(split.value("images").toLongLong(), 1000);
        QCOMPARE(split.value("toolsSchema").toLongLong(), 777);

        // usedTokens uses the existing array口径; total adds the schema.
        const qint64 used = split.value("usedTokens").toLongLong();
        QCOMPARE(used, static_cast<qint64>(TokenEstimator::estimateTokens(messages)));
        QCOMPARE(split.value("totalTokens").toLongLong(), used + 777);

        // Exclusivity: buckets must add up to the used total exactly.
        const qint64 buckets = split.value("systemPrompt").toLongLong()
                             + split.value("conversation").toLongLong()
                             + split.value("toolResults").toLongLong()
                             + split.value("images").toLongLong();
        QCOMPARE(buckets, used);
    }

    // docs/16 §8.3（S1）: 互斥分类。断言三件事：
    //   1) 所有行之和 == estimateTokens(messages)（"桶之和 == 总量"合同）
    //   2) 工具结果按 tool_call_id → 工具名归类，未知工具落 tool.other
    //   3) 压缩标记（system）不与系统提示词混为一桶
    void classifyMessagesBuckets() {
        const auto toolCall = [](const QString& id, const QString& name,
                                 const QString& args) {
            return QJsonObject{{"id", id}, {"type", "function"},
                               {"function", QJsonObject{{"name", name},
                                                        {"arguments", args}}}};
        };

        QJsonArray messages;
        messages.append(msg("system", QString(240, 's')));
        messages.append(msg("system", QStringLiteral(
            "[Context compressed: 4 earlier messages were omitted to fit "
            "the model's context window.]")));

        QJsonObject user = msg("user", QStringLiteral("please read the file"));
        user["files"] = QJsonArray{QJsonObject{{"name", "a.txt"},
                                               {"content", QString(320, 'f')}}};
        user["images"] = QJsonArray{QStringLiteral("data:image/png;base64,QUJD")};
        messages.append(user);

        QJsonObject assistant = msg("assistant", QStringLiteral("reading"));
        assistant["tool_calls"] = QJsonArray{
            toolCall("c1", "read_file", QStringLiteral("{\"path\":\"a.txt\"}")),
            toolCall("c2", "shell_exec", QStringLiteral("{\"cmd\":\"ls\"}")),
            toolCall("c3", "mcp__fs__stat", QStringLiteral("{\"path\":\"b\"}")),
            toolCall("c4", "git_log", QStringLiteral("{}")),
            toolCall("c5", "spawn_agent", QStringLiteral("{\"goal\":\"x\"}")),
            toolCall("c6", "search_knowledge_base", QStringLiteral("{\"q\":\"y\"}")),
            toolCall("c7", "todo_write", QStringLiteral("{\"items\":[]}")),
        };
        messages.append(assistant);

        const QStringList ids{"c1", "c2", "c3", "c4", "c5", "c6", "c7"};
        for (const QString& id : ids) {
            QJsonObject toolMsg = msg("tool", QString(160, 't'));
            toolMsg["tool_call_id"] = id;
            messages.append(toolMsg);
        }

        const QVariantMap rows = TokenEstimator::classifyMessages(messages);
        const auto row = [&rows](const char* key) {
            return rows.value(QString::fromLatin1(key)).toLongLong();
        };

        // 1) 互斥合同
        qint64 sum = 0;
        for (auto it = rows.constBegin(); it != rows.constEnd(); ++it) {
            if (it.key() == QLatin1String("compressedCount")) continue;
            sum += it.value().toLongLong();
        }
        QCOMPARE(sum, static_cast<qint64>(TokenEstimator::estimateTokens(messages)));

        // 2) 分类
        QCOMPARE(rows.value("compressedCount").toInt(), 1);
        QVERIFY(row("systemPrompt") > 0);
        QVERIFY(row("compressed") > 0);
        QVERIFY(row("toolCalls") > 0);
        QVERIFY(row("user") > 0);
        QVERIFY(row("assistant") > 0);
        QCOMPARE(row("images"), qint64(1000));
        QVERIFY(row("files") > 0);
        QVERIFY(row("tool.fileRead") > 0);
        QVERIFY(row("tool.command") > 0);
        QVERIFY(row("tool.mcp") > 0);
        QVERIFY(row("tool.git") > 0);
        QVERIFY(row("tool.subagent") > 0);
        QVERIFY(row("tool.knowledge") > 0);
        QVERIFY(row("tool.other") > 0);
        // 七条工具结果全部落到 <tool.*>，一条都不该留在对话行里
        const qint64 toolTotal = row("tool.fileRead") + row("tool.command")
                               + row("tool.mcp") + row("tool.git")
                               + row("tool.subagent") + row("tool.knowledge")
                               + row("tool.other");
        QVERIFY(toolTotal > 0);

        // 3) 附件正文从用户行剥离（否则"对话"会把附件也算进去）
        const QJsonObject userOnly = msg("user", QStringLiteral("please read the file"));
        QCOMPARE(row("user"),
                 static_cast<qint64>(TokenEstimator::estimateTokens(
                     QJsonArray{userOnly})));
    }

    // S2 预置：工具定义拆分（内置 / MCP）不改动总量口径
    void toolsSchemaSplitMatchesTotal() {
        QJsonArray tools;
        tools.append(QJsonObject{
            {"type", "function"},
            {"function", QJsonObject{{"name", "read_file"},
                                     {"description", "read a file"},
                                     {"parameters", QJsonObject{{"type", "object"}}}}}});
        tools.append(QJsonObject{
            {"type", "function"},
            {"function", QJsonObject{{"name", "mcp__fs__stat"},
                                     {"description", "stat via MCP"},
                                     {"parameters", QJsonObject{{"type", "object"}}}}}});
        int sum = 0;
        for (const auto& t : tools)
            sum += TokenEstimator::estimateToolTokens(t.toObject());
        QCOMPARE(sum, TokenEstimator::estimateToolsSchema(tools));
    }

};

void TestTokenEstimator::estimateBasics() {
    QCOMPARE(TokenEstimator::estimateTokens(QString()), 0);
    QVERIFY(TokenEstimator::estimateTokens("abcdefg") > 0);
    QVERIFY(TokenEstimator::exceedsBudget(QJsonArray{msg("user", QString(100, 'x'))}, 10));
    QCOMPARE(TokenEstimator::remainingBudget(QJsonArray{}, 100), 100);
}

// CJK characters cost ~1.5 chars per token: the estimator must weight them
// accordingly, otherwise long Chinese conversations overflow the real
// context window before compression triggers.
void TestTokenEstimator::estimateCjkWeighted() {
    const QString cn = QStringLiteral("教师简介人文学院助理研究员"); // 13 CJK chars
    QCOMPARE(TokenEstimator::estimateTokens(cn), (13 * 2 + 2) / 3);  // 9, not 13/4=3
    QVERIFY(TokenEstimator::estimateTokens(cn) > cn.length() / 4);

    // CJK punctuation and fullwidth forms count as CJK
    QCOMPARE(TokenEstimator::estimateTokens(QStringLiteral("。！")), (2 * 2 + 2) / 3);

    // Mixed text: Latin part /4 + CJK part /1.5
    QCOMPARE(TokenEstimator::estimateTokens(QStringLiteral("abc中文def")),
             (6 + 3) / 4 + (2 * 2 + 2) / 3);

    // Non-empty text never estimates to zero
    QVERIFY(TokenEstimator::estimateTokens(QStringLiteral("中")) >= 1);
}

// A13: Hangul syllables (U+AC00-U+D7A3) were outside the CJK range, so Korean
// text was estimated at ASCII /4 and under-counted by roughly 5x.
void TestTokenEstimator::estimateHangulWeighted() {
    const QString ko = QStringLiteral("한국어테스트");   // 6 Hangul syllables
    QCOMPARE(ko.length(), 6);
    // ceil(6 / 1.5) = 4 tokens, not 6 / 4 = 1.
    QCOMPARE(TokenEstimator::estimateTokens(ko), (6 * 2 + 2) / 3);
    QVERIFY(TokenEstimator::estimateTokens(ko) > ko.length() / 4);

    // Same-length ASCII keeps the /4 rule.
    QCOMPARE(TokenEstimator::estimateTokens(QString(6, QChar('a'))),
             (6 + 3) / 4);
}

// A13: each emoji is a surrogate pair. Counting both UTF-16 units as Latin
// text yielded ~0.5 token per emoji; a non-BMP code point costs 2 tokens.
void TestTokenEstimator::estimateEmojiSurrogatePairs() {
    const QString emoji = QStringLiteral("😀");
    QCOMPARE(emoji.size(), 2);   // UTF-16 surrogate pair
    const QString four = emoji + emoji + emoji + emoji;
    const int tokens = TokenEstimator::estimateTokens(four);
    QVERIFY2(tokens >= 7 && tokens <= 9,
             qPrintable(QStringLiteral("4 emoji estimated %1 tokens").arg(tokens)));

    // Other characters keep their old accounting next to the pair.
    const int mixed = TokenEstimator::estimateTokens(QStringLiteral("ab") + emoji);
    QCOMPARE(mixed, (2 + 3) / 4 + 2);
}

// A13: compressToFit truncates by QChar index; cutting between the two halves
// of a surrogate pair leaves a lone high surrogate.
void TestTokenEstimator::compressDoesNotSplitSurrogatePair() {
    QJsonArray history;
    history.append(msg("system", QStringLiteral("sys")));
    history.append(msg("user", QStringLiteral("read the emoji file")));

    QJsonObject assistant;
    assistant["role"] = QStringLiteral("assistant");
    assistant["content"] = QString();
    QJsonArray calls;
    QJsonObject call;
    call["id"] = QStringLiteral("call_emoji");
    call["type"] = QStringLiteral("function");
    QJsonObject fn;
    fn["name"] = QStringLiteral("read_file");
    fn["arguments"] = QStringLiteral("{\"path\":\"emoji.json\"}");
    call["function"] = fn;
    calls.append(call);
    assistant["tool_calls"] = calls;
    history.append(assistant);

    const QString emoji = QStringLiteral("😀");   // U+1F600, 2 UTF-16 units
    QJsonObject toolMsg = msg("tool",
        QStringLiteral("x") + emoji.repeated(2000) + QStringLiteral("y"));
    toolMsg["tool_call_id"] = QStringLiteral("call_emoji");
    history.append(toolMsg);
    QVERIFY2(!hasLoneSurrogate(history.last().toObject()
                                   .value("content").toString()),
             "fixture itself is not a valid UTF-16 string");

    const QJsonArray compressed = TokenEstimator::compressToFit(history, 700);
    const QString content = compressed.last().toObject()
                                .value("content").toString();
    QVERIFY2(!hasLoneSurrogate(content),
             "compression cut a surrogate pair, leaving an orphan surrogate");
}

void TestTokenEstimator::compressFittingHistoryUnchanged() {
    QJsonArray history;
    history.append(msg("system", "sys"));
    history.append(msg("user", "hello"));
    history.append(msg("assistant", "hi"));
    QCOMPARE(TokenEstimator::compressToFit(history, 8192).size(), history.size());
}

void TestTokenEstimator::compressKeepsSystemAndSuffix() {
    const int limit = 4000;
    const QJsonArray compressed = TokenEstimator::compressToFit(buildOversizedHistory(), limit);
    QVERIFY(compressed.size() < 161);
    QCOMPARE(compressed.first().toObject().value("role").toString(), QStringLiteral("system"));
    // Kept range is contiguous with the newest message
    QCOMPARE(compressed.last().toObject().value("role").toString(),
             buildOversizedHistory().last().toObject().value("role").toString());
}

void TestTokenEstimator::compressSuffixStartsAtUser() {
    const QJsonArray compressed = TokenEstimator::compressToFit(buildOversizedHistory(), 4000);
    int start = 1;
    if (compressed.at(1).toObject().value("role").toString() == "system")
        ++start; // the omission marker
    QCOMPARE(compressed.at(start).toObject().value("role").toString(), QStringLiteral("user"));
}

void TestTokenEstimator::compressFitsBudget() {
    const QJsonArray compressed = TokenEstimator::compressToFit(buildOversizedHistory(), 4000);
    QVERIFY(TokenEstimator::estimateTokens(compressed) <= 4000);
}

void TestTokenEstimator::compressInsertsMarker() {
    const QJsonArray compressed = TokenEstimator::compressToFit(buildOversizedHistory(), 4000);
    QVERIFY(anyContentContains(compressed, "[Context compressed"));
}

void TestTokenEstimator::compressGiantLastTurnBestEffort() {
    QJsonArray history;
    history.append(msg("system", "sys"));
    history.append(msg("user", QString(50000, 'x')));
    history.append(msg("assistant", "done"));
    // Even when a single turn exceeds the budget it must be kept
    const QJsonArray compressed = TokenEstimator::compressToFit(history, 1000);
    QVERIFY(compressed.size() >= 2);
    QVERIFY(anyContentContains(compressed, "done"));
}

// docs/16 §8.3（S3）: 省略条数与截断标记必须能报给调用方（TopBar 的
// "已压缩 N 条"），而默认参数下行为与旧实现完全一致。
void TestTokenEstimator::compressReportsOmittedAndTruncated() {
    // ① 整轮省略：omittedMessages = 被丢掉的消息数（不含保留的 system）
    int omitted = -1;
    bool truncated = true;
    const QJsonArray history = buildOversizedHistory();
    const QJsonArray squeezed =
        TokenEstimator::compressToFit(history, 4000, &omitted, &truncated);
    QVERIFY(omitted > 0);
    // 输出 = 保留的 system + 1 条省略标记 + 被保留的后缀
    QCOMPARE(squeezed.size(), history.size() - omitted + 1);
    QVERIFY(anyContentContains(squeezed, "[Context compressed"));

    // ② 无需压缩：两个出参都归零
    QJsonArray small;
    small.append(msg("system", "sys"));
    small.append(msg("user", "hi"));
    small.append(msg("assistant", "hello"));
    int omittedSmall = -1;
    bool truncatedSmall = true;
    const QJsonArray untouched =
        TokenEstimator::compressToFit(small, 100000, &omittedSmall, &truncatedSmall);
    QCOMPARE(untouched.size(), small.size());
    QCOMPARE(omittedSmall, 0);
    QVERIFY(!truncatedSmall);

    // ③ 原地截断：消息数不变但内容被切，truncated 必须为 true
    QJsonArray giant;
    giant.append(msg("system", "sys"));
    giant.append(msg("user", "hi"));
    // 只有 assistant 可控可截（用户自己的话永远不动），所以让它成为超大头。
    giant.append(msg("assistant", QString(20000, 'a')));
    int omittedGiant = -1;
    bool truncatedGiant = false;
    const QJsonArray cut =
        TokenEstimator::compressToFit(giant, 1000, &omittedGiant, &truncatedGiant);
    QVERIFY(truncatedGiant);
    QVERIFY(anyContentContains(cut, "truncated to fit the context window"));

    // ④ 不传默认参数：编译与行为都不受影响（旧调用点零改动）
    const QJsonArray legacy = TokenEstimator::compressToFit(history, 4000);
    QCOMPARE(legacy.size(), squeezed.size());
}

// A6：最新的 assistant 轮只有 tool_calls（content 为空），超大的 tool 结果
// 才是压垮预算的元凶。旧实现只截断 content 非空的 assistant，找不到就
// break，函数返回超预算数组；新实现应兜底截断当前轮的 tool 结果，同时
// 保留 role / tool_call_id 与 assistant 的 tool_calls（配对不破坏）。
void TestTokenEstimator::compressTruncatesCurrentToolResult() {
    QJsonArray history;
    history.append(msg("system", QStringLiteral("sys")));
    history.append(msg("user", QStringLiteral("read the file")));

    QJsonObject assistant;
    assistant["role"] = QStringLiteral("assistant");
    assistant["content"] = QStringLiteral("");
    QJsonArray calls;
    QJsonObject call;
    call["id"] = QStringLiteral("call_1");
    call["type"] = QStringLiteral("function");
    QJsonObject fn;
    fn["name"] = QStringLiteral("read_file");
    fn["arguments"] = QStringLiteral("{\"path\":\"big.txt\"}");
    call["function"] = fn;
    calls.append(call);
    assistant["tool_calls"] = calls;
    history.append(assistant);

    QJsonObject toolMsg = msg("tool", QString(6000, QChar('t')));
    toolMsg["tool_call_id"] = QStringLiteral("call_1");
    history.append(toolMsg);

    const int before = TokenEstimator::estimateTokens(history);
    QVERIFY2(before > 700, qPrintable(QStringLiteral("fixture too small: %1").arg(before)));

    const QJsonArray compressed = TokenEstimator::compressToFit(history, 700);
    QVERIFY2(TokenEstimator::estimateTokens(compressed) <= 700,
             qPrintable(QStringLiteral("still over budget: %1")
                            .arg(TokenEstimator::estimateTokens(compressed))));

    const QJsonObject last = compressed.last().toObject();
    QCOMPARE(last.value("role").toString(), QStringLiteral("tool"));
    QCOMPARE(last.value("tool_call_id").toString(), QStringLiteral("call_1"));
    QVERIFY(last.value("content").toString()
                .contains(QStringLiteral("[truncated")));
    QVERIFY(last.value("content").toString().size() < 6000);

    // assistant 的 tool_calls 原样保留（配对的两端都还在）。
    const QJsonObject assistantOut = compressed.at(2).toObject();
    QCOMPARE(assistantOut.value("role").toString(), QStringLiteral("assistant"));
    const QJsonArray callsOut = assistantOut.value("tool_calls").toArray();
    QCOMPARE(callsOut.size(), 1);
    QCOMPARE(callsOut.first().toObject().value("id").toString(),
             QStringLiteral("call_1"));
    QCOMPARE(callsOut.first().toObject().value("function").toObject()
                 .value("name").toString(),
             QStringLiteral("read_file"));
}

void TestTokenEstimator::compressNoUserAnchorUnchanged() {
    QJsonArray history;
    history.append(msg("system", "sys"));
    history.append(msg("assistant", "only assistant"));
    QCOMPARE(TokenEstimator::compressToFit(history, 10).size(), history.size());
}

// docs/35 P4.1: the sub-agent's history has exactly one user message, so the
// generic compressor is a no-op there. The round-aware variant must drop the
// oldest complete assistant→tool pairs, keep the goal, and report the cuts.
void TestTokenEstimator::compressToolRoundsDropsOldestKeepsPairs() {
    const QJsonArray history = buildSubAgentHistory(16, 1200);
    QVERIFY(TokenEstimator::estimateTokens(history) > 4000);

    int omitted = 0;
    int truncated = 0;
    const QJsonArray compressed = TokenEstimator::compressToolRoundsToFit(
        history, 1500, &omitted, &truncated);

    QVERIFY2(omitted > 0, "old rounds were not dropped");
    QCOMPARE(truncated, 0);
    QVERIFY2(TokenEstimator::estimateTokens(compressed) <= 1500,
             "compressed history still exceeds the budget");
    QCOMPARE(compressed.first().toObject().value("role").toString(),
             QStringLiteral("system"));
    QCOMPARE(compressed.at(1).toObject().value("role").toString(),
             QStringLiteral("user"));
    QVERIFY(anyContentContains(compressed, QStringLiteral("[Context compressed")));
    // The newest round survives verbatim.
    QCOMPARE(compressed.last().toObject().value("content").toString(),
             history.last().toObject().value("content").toString());
    // Tool pairing: no "tool" message without its requesting assistant turn.
    for (int i = 0; i < compressed.size(); ++i) {
        const QJsonObject m = compressed.at(i).toObject();
        if (m.value("role").toString() != QLatin1String("tool")) continue;
        const QString id = m.value("tool_call_id").toString();
        bool answered = false;
        for (int j = i - 1; j >= 0 && !answered; --j) {
            for (const auto& c : compressed.at(j).toObject()
                                     .value("tool_calls").toArray()) {
                if (c.toObject().value("id").toString() == id) {
                    answered = true;
                    break;
                }
            }
        }
        QVERIFY2(answered, qPrintable(QStringLiteral(
            "orphan tool message at %1 (id %2)").arg(i).arg(id)));
    }
}

void TestTokenEstimator::compressToolRoundsTruncatesHugeResult() {
    const QJsonArray history = buildSubAgentHistory(1, 20000);
    int omitted = 0;
    int truncated = 0;
    const QJsonArray compressed = TokenEstimator::compressToolRoundsToFit(
        history, 600, &omitted, &truncated);

    // Only one group exists, so nothing can be dropped — the huge result
    // itself must be shrunk, and the assistant/tool pair must stay intact.
    QCOMPARE(omitted, 0);
    QVERIFY2(truncated >= 1, "oversized tool result was not truncated");
    QCOMPARE(compressed.size(), history.size());
    QVERIFY(anyContentContains(compressed,
                               QStringLiteral("[truncated to fit context]")));
    QVERIFY2(TokenEstimator::estimateTokens(compressed) <= 600,
             "truncation did not reach the budget");
}

void TestTokenEstimator::compressToolRoundsFittingHistoryUnchanged() {
    const QJsonArray history = buildSubAgentHistory(2, 10);
    int omitted = 0;
    int truncated = 0;
    const QJsonArray compressed = TokenEstimator::compressToolRoundsToFit(
        history, 100000, &omitted, &truncated);
    QCOMPARE(omitted, 0);
    QCOMPARE(truncated, 0);
    QCOMPARE(compressed.size(), history.size());
    QCOMPARE(compressed.first().toObject().value("content").toString(),
             history.first().toObject().value("content").toString());
    QCOMPARE(compressed.last().toObject().value("content").toString(),
             history.last().toObject().value("content").toString());
}

QTEST_GUILESS_MAIN(TestTokenEstimator)
#include "test_token_estimator.moc"
