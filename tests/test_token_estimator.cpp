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
    void estimateBasics();
    void estimateCjkWeighted();
    void compressFittingHistoryUnchanged();
    void compressKeepsSystemAndSuffix();
    void compressSuffixStartsAtUser();
    void compressFitsBudget();
    void compressInsertsMarker();
    void compressGiantLastTurnBestEffort();
    void compressNoUserAnchorUnchanged();
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

QTEST_GUILESS_MAIN(TestTokenEstimator)
#include "test_token_estimator.moc"
