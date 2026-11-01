// T8 episodic memory service unit tests.
#include "core/agent/EpisodicService.h"
#include "core/memory/SqliteMemory.h"
#include "core/agent/LLMTypes.h"
#include "infrastructure/llm/IAIService.h"

#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QTimer>
#include <QEventLoop>

using namespace NexusAI;

class FakeLLM : public IAIService {
    Q_OBJECT
public:
    QVector<QJsonArray> seenMessages;
    QVector<LLMResponse> script;
    int idx{0};
    // docs/24 H4: the first N calls record the request but emit nothing —
    // simulates LLMServiceBase silently superseding an in-flight request.
    int silentFirstCalls{0};

    void chatComplete(const QJsonArray& messages, const QJsonArray&) override {
        seenMessages.append(messages);
        LLMResponse resp;
        if (idx < script.size()) resp = script[idx++];
        else if (!script.isEmpty()) resp = script.last();
        resp.success = true;
        if (seenMessages.size() <= silentFirstCalls) return;
        emit responseReady(resp);
    }
    void cancel() override {}
    void setApiKey(const QString&) override {}
    void setModel(const QString&) override {}
    void setBaseUrl(const QString&) override {}
};

class TestEpisodic : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        // Use a temp file for SQLite so FTS5 works (":memory:" has issues
        // with FTS5 external content tables in some Qt versions).
        QVERIFY(m_tmpDir.isValid());
        m_dbPath = m_tmpDir.filePath("episodic.db");
    }

    void cleanup() {
        m_seenMessages.clear();
    }

    void disabledByDefault() {
        EpisodicService svc;
        QVERIFY(!svc.isEnabled());
        QVERIFY(svc.retrieveRelevantSummaries(QStringLiteral("test")).isEmpty());
    }

    void retrieveReturnsEmptyWhenDisabled() {
        auto* mem = makeMemory();
        EpisodicService svc;
        svc.setMemory(mem);
        svc.setEnabled(false);
        QVERIFY(svc.retrieveRelevantSummaries(QStringLiteral("query")).isEmpty());
        delete mem;
    }

    // docs/26 I4: 会话标题生成以 summaryStored 为链式触发信号（串行在摘要
    // 之后）。「不走摘要」的早退（禁用/无服务）也必须发完成信号，否则标题
    // 链永久挂起——真实用户反馈：episodic 默认关时标题永远是截断的首条消息。
    void disabledServiceStillSignalsCompletion() {
        auto* mem = makeMemory();
        EpisodicService svc;
        svc.setMemory(mem);
        svc.setEnabled(false);

        QSignalSpy spy(&svc, &EpisodicService::summaryStored);
        QJsonArray messages;
        messages.append(QJsonObject{{"role", "user"}, {"content", "你好"}});
        svc.generateAndStoreSummary(QStringLiteral("test-session"), messages);

        QCOMPARE(spy.count(), 1);
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toString(), QStringLiteral("test-session"));
        QCOMPARE(args.at(1).toBool(), false);
        delete mem;
    }

    void retrieveReturnsResultsWhenEnabled() {
        auto* mem = makeMemory();
        EpisodicService svc;
        svc.setMemory(mem);
        svc.setEnabled(true);

        // Seed an episodic entry.
        QJsonObject episode;
        episode["summary"] = QStringLiteral("用户询问了 Qt 信号槽机制的工作原理");
        episode["tags"] = QStringLiteral("Qt,信号槽");
        mem->saveEpisodic(QStringLiteral("session-1"), episode);

        const QString result = svc.retrieveRelevantSummaries(QStringLiteral("Qt 信号槽"));
        QVERIFY(!result.isEmpty());
        QVERIFY(result.contains(QStringLiteral("Qt")));
        QVERIFY(result.contains(QStringLiteral("历史会话摘要")));

        delete mem;
    }

    void injectionTokenBudgetCapped() {
        auto* mem = makeMemory();
        EpisodicService svc;
        svc.setMemory(mem);
        svc.setEnabled(true);

        // Seed many large summaries to exceed the 1500 token budget.
        for (int i = 0; i < 20; ++i) {
            QJsonObject episode;
            episode["summary"] = QStringLiteral("会话 %1 摘要：这是一个很长的摘要内容"
                "用于测试注入预算钳制机制确保总token数不超过1500的限制。"
                "重复内容以增加体积：").arg(i) + QString(200, QChar('x'));
            episode["tags"] = QStringLiteral("test,budget");
            mem->saveEpisodic(QStringLiteral("session-%1").arg(i), episode);
        }

        const QString result = svc.retrieveRelevantSummaries(QStringLiteral("test"), 20);
        QVERIFY(!result.isEmpty());

        // Verify the result is within budget (1500 tokens + header overhead).
        const int resultTokens = TokenEstimator::estimateTokens(result);
        QVERIFY2(resultTokens <= EpisodicService::kMaxInjectionTokens + 200,
                 qPrintable(QStringLiteral("Budget exceeded: %1 tokens").arg(resultTokens)));

        delete mem;
    }

    void summaryGenerationFiresWhenEnabled() {
        auto* mem = makeMemory();
        auto* llm = new FakeLLM;
        LLMResponse scripted;
        scripted.content = QStringLiteral("Session about Qt signal slot mechanism");
        scripted.tokensUsed = 10;
        scripted.success = true;
        llm->script.append(scripted);

        EpisodicService svc;
        svc.setMemory(mem);
        svc.setLlmService(llm);
        svc.setEnabled(true);

        QSignalSpy spy(&svc, &EpisodicService::summaryStored);

        QJsonArray messages;
        messages.append(QJsonObject{{"role", "system"}, {"content", "system prompt"}});
        messages.append(QJsonObject{{"role", "user"}, {"content", "什么是 Qt？"}});
        messages.append(QJsonObject{{"role", "assistant"}, {"content", "Qt 是一个跨平台框架"}});

        svc.generateAndStoreSummary(QStringLiteral("test-session"), messages);

        // The fake LLM responds synchronously, so the spy should fire.
        QVERIFY(spy.wait(1000) || spy.count() > 0);
        QCOMPARE(spy.count(), 1);
        QVERIFY(spy.takeFirst().at(1).toBool());

        // Verify the summary was stored.
        const QJsonArray found = mem->searchEpisodic(QStringLiteral("Qt"), 10);
        QCOMPARE(found.size(), 1);

        delete llm;
        delete mem;
    }

    // docs/24 H4: the service keeps one in-flight request — the second
    // summary silently supersedes the first, and the single response must
    // land on the right session (the old FIFO pairing stored it under BOTH
    // sessions).
    void supersededSummaryDoesNotMisroute() {
        auto* mem = makeMemory();
        auto* llm = new FakeLLM;
        llm->silentFirstCalls = 1;   // request A is superseded, never answers
        LLMResponse scripted;
        scripted.success = true;
        scripted.content = QStringLiteral("会话B的关键结论摘要");
        llm->script.append(scripted);

        EpisodicService svc;
        svc.setMemory(mem);
        svc.setLlmService(llm);
        svc.setEnabled(true);
        QSignalSpy spy(&svc, &EpisodicService::summaryStored);

        QJsonArray messages;
        messages.append(QJsonObject{{"role", "user"}, {"content", "hello"}});
        svc.generateAndStoreSummary(QStringLiteral("session-A"), messages);
        svc.generateAndStoreSummary(QStringLiteral("session-B"), messages);

        // A reported as dropped, B stored — nothing misattributed.
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("session-A"));
        QCOMPARE(spy.at(0).at(1).toBool(), false);
        QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("session-B"));
        QCOMPARE(spy.at(1).at(1).toBool(), true);
        QCOMPARE(mem->searchEpisodic(QStringLiteral("会话B"), 10).size(), 1);

        delete llm;
        delete mem;
    }

    // docs/24 H4: a superseded request never emits — the deadline must
    // release the pending entry and report the drop instead of leaking it.
    void summaryTimeoutReleasesPending() {
        auto* mem = makeMemory();
        auto* llm = new FakeLLM;
        llm->silentFirstCalls = 1;   // this request will never be answered

        EpisodicService svc;
        svc.setMemory(mem);
        svc.setLlmService(llm);
        svc.setEnabled(true);
        svc.setSummaryTimeoutMs(20);
        QSignalSpy spy(&svc, &EpisodicService::summaryStored);

        QJsonArray messages;
        messages.append(QJsonObject{{"role", "user"}, {"content", "hello"}});
        svc.generateAndStoreSummary(QStringLiteral("session-T"), messages);
        QCOMPARE(spy.count(), 0);   // request in flight, nothing yet

        QTRY_COMPARE(spy.count(), 1);   // deadline fires
        QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("session-T"));
        QCOMPARE(spy.at(0).at(1).toBool(), false);

        delete llm;
        delete mem;
    }

    void summarySkippedWhenDisabled() {
        auto* mem = makeMemory();
        auto* llm = new FakeLLM;

        EpisodicService svc;
        svc.setMemory(mem);
        svc.setLlmService(llm);
        svc.setEnabled(false);

        QJsonArray messages;
        messages.append(QJsonObject{{"role", "user"}, {"content", "hello"}});

        svc.generateAndStoreSummary(QStringLiteral("s1"), messages);

        QCOMPARE(llm->seenMessages.size(), 0);

        delete llm;
        delete mem;
    }

    void retrievalFailureDoesNotThrow() {
        EpisodicService svc;
        svc.setEnabled(true);
        // No memory set — should return empty, not crash.
        QVERIFY(svc.retrieveRelevantSummaries(QStringLiteral("test")).isEmpty());
    }

    void summaryFailureDoesNotBlock() {
        auto* mem = makeMemory();
        auto* llm = new FakeLLM;
        // Script an error response.
        LLMResponse errResp;
        errResp.success = false;
        errResp.error = QStringLiteral("LLM error");
        llm->script.append(errResp);

        EpisodicService svc;
        svc.setMemory(mem);
        svc.setLlmService(llm);
        svc.setEnabled(true);

        QSignalSpy spy(&svc, &EpisodicService::summaryStored);

        QJsonArray messages;
        messages.append(QJsonObject{{"role", "user"}, {"content", "hello"}});

        svc.generateAndStoreSummary(QStringLiteral("s1"), messages);

        QVERIFY(spy.wait(1000) || spy.count() > 0);
        QCOMPARE(spy.count(), 1);
        QVERIFY(!spy.takeFirst().at(1).toBool());

        // No summary should be stored.
        QCOMPARE(mem->searchEpisodic(QStringLiteral("test"), 10).size(), 0);

        delete llm;
        delete mem;
    }

    void trigramMatchesCjkSubstring() {
        auto* mem = makeMemory();
        QJsonObject episode;
        episode["summary"] = QStringLiteral("用户询问了 Qt 信号槽机制的工作原理");
        episode["tags"] = QStringLiteral("Qt,信号槽");
        mem->saveEpisodic(QStringLiteral("session-1"), episode);

        // 3-char CJK query: trigram tokenizer should substring-match.
        const QJsonArray results = mem->searchEpisodic(QStringLiteral("信号槽"), 10);
        QCOMPARE(results.size(), 1);
        QVERIFY(results.at(0).toObject().value("summary").toString()
                    .contains(QStringLiteral("信号槽")));

        delete mem;
    }

    void shortQueryFallsBackToLike() {
        auto* mem = makeMemory();
        QJsonObject episode;
        episode["summary"] = QStringLiteral("讨论了信号槽的连接方式");
        episode["tags"] = QStringLiteral("Qt");
        mem->saveEpisodic(QStringLiteral("s1"), episode);

        // 2-char query: trigram cannot answer, LIKE fallback should hit.
        const QJsonArray results = mem->searchEpisodic(QStringLiteral("信号"), 10);
        QCOMPARE(results.size(), 1);
        QVERIFY(results.at(0).toObject().value("summary").toString()
                    .contains(QStringLiteral("信号槽")));

        delete mem;
    }

    void specialCharsDoNotCrashSearch() {
        auto* mem = makeMemory();
        QJsonObject episode;
        episode["summary"] = QStringLiteral("normal summary text");
        episode["tags"] = QStringLiteral("tag");
        mem->saveEpisodic(QStringLiteral("s1"), episode);

        // FTS5 special characters in the query must not cause a syntax error
        // (the quoting in searchEpisodic should neutralize them).
        const QJsonArray results = mem->searchEpisodic(QStringLiteral("a*b(c)"), 10);
        QVERIFY(results.size() == 0); // no match, but no crash either

        delete mem;
    }

    // docs/25 B2: unified score merge — FTS hits carry 1.0 (an explicit
    // keyword match outranks any cosine), semantic hits their cosine, a row
    // hit by both channels appears exactly once, and the merged set is
    // topK-capped.
    void mergedRetrievalDedupesAndRanks() {
        auto* mem = makeMemory();
        EpisodicService svc;
        svc.setMemory(mem);
        svc.setEnabled(true);
        // Deterministic 2-D embedder: the query vector is always {1, 0}.
        svc.setEmbedder([](const QString&) { return QVector<float>{1.0f, 0.0f}; });

        const auto seed = [&](const QString& summary) {
            QJsonObject episode;
            episode["summary"] = summary;
            episode["tags"] = QStringLiteral("test");
            return mem->saveEpisodic(QStringLiteral("s"), episode);
        };
        const qint64 bothId = seed(QStringLiteral("zeta keyword both channels"));
        const qint64 semId = seed(QStringLiteral("zeta semantic only entry"));
        const qint64 weakId = seed(QStringLiteral("zeta weak semantic entry"));

        const auto setEmb = [&](qint64 id, float x, float y) {
            const QVector<float> v{x, y};
            const QByteArray blob(reinterpret_cast<const char*>(v.constData()),
                                  int(v.size() * sizeof(float)));
            QVERIFY(mem->updateEpisodicEmbedding(id, blob));
        };
        setEmb(bothId, 1.0f, 0.0f);   // cosine 1.0 with the query AND FTS hit
        setEmb(semId, 1.0f, 0.1f);    // cosine ~0.995, no keyword in summary
        setEmb(weakId, 0.6f, 0.8f);   // cosine 0.6, no keyword in summary

        // "keyword" FTS-matches only bothId; every seeded row competes on
        // the semantic channel via the shared embedder vector.
        const QJsonArray top3 = svc.retrieveRaw(QStringLiteral("keyword"), 3);
        QCOMPARE(top3.size(), 3);
        QCOMPARE(top3.at(0).toObject().value("id").toVariant().toLongLong(), bothId);
        QCOMPARE(top3.at(0).toObject().value("score").toDouble(), 1.0);
        QCOMPARE(top3.at(1).toObject().value("id").toVariant().toLongLong(), semId);
        QCOMPARE(top3.at(2).toObject().value("id").toVariant().toLongLong(), weakId);
        QVERIFY(top3.at(1).toObject().value("score").toDouble()
                    > top3.at(2).toObject().value("score").toDouble());
        // Deduped: bothId was hit by BOTH channels but appears once.
        int bothCount = 0;
        for (const auto& v : top3)
            if (v.toObject().value("id").toVariant().toLongLong() == bothId) ++bothCount;
        QCOMPARE(bothCount, 1);
        // topK cap: the weak row drops out of a top-2 request.
        QCOMPARE(svc.retrieveRaw(QStringLiteral("keyword"), 2).size(), 2);

        delete mem;
    }

    // docs/25 B3: with an embedder configured the query round-trip runs on a
    // QtConcurrent worker; the result is queued back to the service thread
    // and delivered via retrievalReady with the matching request id.
    void asyncRetrievalDeliversViaSignal() {
        auto* mem = makeMemory();
        EpisodicService svc;
        svc.setMemory(mem);
        svc.setEnabled(true);
        svc.setEmbedder([](const QString&) { return QVector<float>{1.0f, 0.0f}; });

        QJsonObject episode;
        episode["summary"] = QStringLiteral("zeta async semantic summary");
        episode["tags"] = QStringLiteral("t");
        const qint64 id = mem->saveEpisodic(QStringLiteral("s"), episode);
        const QVector<float> v{1.0f, 0.0f};
        const QByteArray blob(reinterpret_cast<const char*>(v.constData()),
                              int(v.size() * sizeof(float)));
        QVERIFY(mem->updateEpisodicEmbedding(id, blob));

        QSignalSpy spy(&svc, &EpisodicService::retrievalReady);
        const quint64 rid = svc.retrieveRelevantSummariesAsync(
            QStringLiteral("async semantic"));
        QVERIFY(rid > 0);
        QTRY_COMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toULongLong(), rid);
        QVERIFY(!spy.at(0).at(1).toString().isEmpty());   // injection built

        // Each request gets its own id — two in flight, both delivered.
        const quint64 rid2 = svc.retrieveRelevantSummariesAsync(
            QStringLiteral("async semantic"));
        QVERIFY(rid2 > rid);
        QTRY_COMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toULongLong(), rid2);

        delete mem;
    }

    // docs/25 B3: without an embedder the retrieval is a local FTS scan —
    // retrievalReady fires INLINE (no worker hop). Not-applicable requests
    // (empty query / disabled service) return 0 and never emit.
    void asyncRetrievalWithoutEmbedderIsInline() {
        auto* mem = makeMemory();
        EpisodicService svc;
        svc.setMemory(mem);
        svc.setEnabled(true);

        QJsonObject episode;
        episode["summary"] = QStringLiteral("zeta inline fts summary");
        episode["tags"] = QStringLiteral("t");
        mem->saveEpisodic(QStringLiteral("s"), episode);

        QSignalSpy spy(&svc, &EpisodicService::retrievalReady);
        const quint64 rid = svc.retrieveRelevantSummariesAsync(QStringLiteral("zeta"));
        QVERIFY(rid > 0);
        QCOMPARE(spy.count(), 1);   // inline: no event-loop round needed
        QCOMPARE(spy.at(0).at(0).toULongLong(), rid);
        QVERIFY(!spy.at(0).at(1).toString().isEmpty());

        // Empty query → 0, no signal.
        QCOMPARE(svc.retrieveRelevantSummariesAsync(QString()), quint64(0));
        QCOMPARE(spy.count(), 1);

        // Disabled service → 0, no signal.
        svc.setEnabled(false);
        QCOMPARE(svc.retrieveRelevantSummariesAsync(QStringLiteral("zeta")), quint64(0));
        QCOMPARE(spy.count(), 1);

        delete mem;
    }

    // M1: listEpisodic enumerates all stored episodes, newest first.
    void listEpisodicReturnsNewestFirst() {
        auto* mem = makeMemory();
        for (int i = 0; i < 3; ++i) {
            QJsonObject episode;
            episode["summary"] = QStringLiteral("summary %1").arg(i);
            episode["tags"] = QStringLiteral("t%1").arg(i);
            mem->saveEpisodic(QStringLiteral("s%1").arg(i), episode);
        }
        const QJsonArray all = mem->listEpisodic(200);
        QCOMPARE(all.size(), 3);
        QCOMPARE(all.at(0).toObject().value("summary").toString(),
                 QStringLiteral("summary 2"));
        delete mem;
    }

    // M1: deleteEpisodic forgets exactly one entry (and returns false for a
    // missing id).
    void deleteEpisodicForgetsOne() {
        auto* mem = makeMemory();
        QJsonObject episode;
        episode["summary"] = QStringLiteral("keep me");
        mem->saveEpisodic(QStringLiteral("s1"), episode);
        episode["summary"] = QStringLiteral("forget me");
        mem->saveEpisodic(QStringLiteral("s1"), episode);

        const QJsonArray before = mem->listEpisodic(200);
        QCOMPARE(before.size(), 2);
        const qint64 id = before.at(0).toObject().value("id").toInt();
        QVERIFY(mem->deleteEpisodic(id));
        QCOMPARE(mem->listEpisodic(200).size(), 1);
        QVERIFY(!mem->deleteEpisodic(id)); // already gone
        delete mem;
    }

    // M1: deleteAllEpisodic wipes the store and reports the row count.
    void deleteAllEpisodicWipesStore() {
        auto* mem = makeMemory();
        for (int i = 0; i < 3; ++i) {
            QJsonObject episode;
            episode["summary"] = QStringLiteral("s%1").arg(i);
            mem->saveEpisodic(QStringLiteral("sess"), episode);
        }
        QCOMPARE(mem->deleteAllEpisodic(), 3);
        QCOMPARE(mem->listEpisodic(200).size(), 0);
        delete mem;
    }

private:
    SqliteMemory* makeMemory() {
        // Use a unique temp file for each memory instance to avoid cross-test
        // contamination from the shared AppData database.
        const QString path = m_tmpDir.filePath(
            QStringLiteral("episodic_%1.db").arg(++m_dbCounter));
        return new SqliteMemory(path);
    }

    QTemporaryDir m_tmpDir;
    QString m_dbPath;
    int m_dbCounter{0};
    QVector<QJsonArray> m_seenMessages;
};

QTEST_MAIN(TestEpisodic)
#include "test_episodic.moc"
