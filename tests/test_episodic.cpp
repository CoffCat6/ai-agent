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

    void chatComplete(const QJsonArray& messages, const QJsonArray&) override {
        seenMessages.append(messages);
        LLMResponse resp;
        if (idx < script.size()) resp = script[idx++];
        else if (!script.isEmpty()) resp = script.last();
        resp.success = true;
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
