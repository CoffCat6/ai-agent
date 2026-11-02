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
    void compressFittingHistoryUnchanged();
    void compressKeepsSystemAndSuffix();
    void compressSuffixStartsAtUser();
    void compressFitsBudget();
    void compressInsertsMarker();
    void compressGiantLastTurnBestEffort();
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
