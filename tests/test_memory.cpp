// SqliteMemory: message/session persistence and FTS5 episodic search.
#include "core/memory/SqliteMemory.h"
#include "core/rag/KnowledgeChunk.h"
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QFile>

#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>

using namespace NexusAI;

namespace {

QJsonObject msg(const QString& role, const QString& content) {
    QJsonObject m;
    m["role"] = role;
    m["content"] = content;
    return m;
}

// Batch 6 T1: token_usage 明细 fixture（驼峰键对齐 logTokenUsage 契约）。
// 固定 requestId/sessionId，可变 model/timestamp/方向 token/费用/延迟。
QVariantMap usageRec(const QString& model, qint64 ts, int in, int out,
                     int cache, int cost, qint64 latency = 100) {
    QVariantMap r;
    r["sessionId"] = QStringLiteral("s-range");
    r["model"] = model;
    r["requestId"] = 42;
    r["timestamp"] = ts;
    r["inputTokens"] = in;
    r["outputTokens"] = out;
    r["cacheHitTokens"] = cache;
    r["costCents"] = cost;
    r["latencyMs"] = latency;
    return r;
}

} // namespace

class TestMemory : public QObject {
    Q_OBJECT

private slots:
    // Batch 30 (F6): attached images persist through the metadata column
    // and are promoted back to the message top level on load.
    void imageMessagesRoundTrip() {
        SqliteMemory memory(":memory:");

        const QJsonArray images{
            QStringLiteral("data:image/jpeg;base64,QUJD")};
        QJsonObject msg{
            {"role", "user"},
            {"content", "what is in this picture?"},
            {"images", images}};
        memory.saveMessage(QStringLiteral("s-img"), msg);
        memory.saveMessage(QStringLiteral("s-img"), QJsonObject{
            {"role", "assistant"}, {"content", "a red square"}});

        const QJsonArray loaded = memory.loadMessages(QStringLiteral("s-img"));
        QCOMPARE(loaded.size(), 2);
        const QJsonObject userMsg = loaded.at(0).toObject();
        QCOMPARE(userMsg.value("role").toString(), QStringLiteral("user"));
        QVERIFY(userMsg.contains("images"));
        QCOMPARE(userMsg.value("images").toArray().size(), 1);
        QCOMPARE(userMsg.value("images").toArray().at(0).toString(),
                 QStringLiteral("data:image/jpeg;base64,QUJD"));
        QVERIFY(!loaded.at(1).toObject().contains("images"));
    }

    void messageRoundTrip();
    void loadMessagesKeepsNewest();
    void sessionBookkeepingAndClear();
    void sessionDeleteAndRename();
    void fileCheckpointLifecycle();
    void fileCheckpointSessionBudgetDegradesToPathOnly();
    void fileCheckpointPrunedByAgeAndSessionPurge();
    // docs/23 G1: high-frequency paths stay index-backed; the user_version
    // stamp gates the FTS backfill to the upgrade path.
    void sessionQueriesUseIndexes();
    void oldDatabaseUpgradeRestampsAndBackfills();
    // docs/25 B1: idx_episodic_ts / idx_episodic_session are created on
    // fresh databases and re-added in place (IF NOT EXISTS) when an older
    // database predating them is reopened.
    void episodicIndexesPresentAndUpgraded();
    // docs/25 B4: clear()/purgeDeletedSessions() run inside one transaction
    // — observable behavior unchanged (clear wipes messages+episodic of the
    // session, purge hard-deletes only the expired soft-deleted sessions).
    void clearAndPurgeStayAtomic();
    // docs/23 G2: WAL + NORMAL durability for the multi-connection topology.
    void walJournalModeActive();
    // docs/23 G3: time-based retention for audit_log / token_usage.
    void retentionPrunesAuditAndTokenUsage();
    // docs/23 G6: clear() cascades to trace nodes like deleteSession.
    void clearRemovesTraceNodes();
    // docs/23 G5: a failing loadMessages query is logged, not silent.
    void loadMessagesFailureIsLoggedNotSilent();
    // docs/23 G7: soft delete → hidden from list/search → undo → purge.
    void deletedSessionRoundTrip();
    // docs/23 D1: VACUUM INTO snapshot round-trip, rotation, read-only peek.
    void backupRoundTripInspectAndRotate();
    // docs/24 R1: restoreFromBackup happy path + corrupt-snapshot refusal.
    void restoreFromBackupRoundTrip();
    void restoreFromBackupRejectsCorruptSnapshot();
    void episodicFullTextSearch();
    // docs/14 L3 regression: a database whose episodic_fts predates the
    // trigram decision must be upgraded in place the next time it is opened.
    void episodicFtsUpgradesToTrigramOnOpen();
    // docs/21 分叉为新会话：主键回传、线性截断要连时间线一起删、按回答序号分叉
    // （含二次分叉）、以及中间那版消息树的拉平迁移。
    void saveMessageReturnsPrimaryKeyOrZero();
    void truncateLastTurnPrunesPersistedTraceNodes();
    void forkSessionAtAnswerCopiesThroughThatAnswer();
    void forkedSessionForksAgainKeepingProcess();
    void treeDatabaseFlattensToLinearHistory();
    void auditTrail();
    void tokenStatsAggregate();
    void tokenStatsEmptyWhenNoData();
    // docs/16 §8.3: session-scoped token breakdown (TopBar hover popup).
    void sessionTokenBreakdown();
    // Batch 6 T1: 起止时间戳范围聚合 + 明细分页（token_usage 口径）。
    void tokenStatsDetailedCustomRange();
    void tokenUsageRecordsPagination();
    void tokenUsageRecordsMatchAggregates();
    void costStatsAndBySourceCustomRange();
    // 批次六审查 M1: byDay 行上限——范围模式 366 行（跨 >31 天范围
    // 不截断）/ recentDays 回退窗口 31 行（既有语义回归锁定）。
    void byDayRangeModeLimitSemantics();
    // docs/17 A0: projects + session ownership
    void projectSessionLifecycle();
    // docs/17 P2: project rename (display name only)
    void projectRename();
    void traceNodeRoundTrip();
    // Batch 6: knowledge persistence v2
    void knowledgeSchemaV1Migrates();
    void knowledgeChunkPersistence();
    void knowledgeCascadeDelete();
    // Batch 7: Conversation/Memory consistency
    void sqliteConnectionNameCleanedUp();
    void workspaceIdColumnReserved();
    // 日志增强: usage trail persistence (Batch 12 前日志增强)
    void usageLogRoundTripAndPrune();
    // usage timeline: AgentViewModel 持有 IMemory*，loadUsageEvents 必须经
    // 接口虚方法接线——若 SqliteMemory 的签名漂移（如丢失 const）只是
    // hide 而非 override，接口调用会拿默认空数组。
    void usageEventsReachableThroughInterface();
    // Batch 17: cross-session message search (FEAT-1701/1702)
    void searchMessagesFindsAcrossSessions();
    void searchMessagesSyncsOnDelete();
    void searchMessagesShortKeywordLikeFallback();
    void messagesFtsMigrationSelfHeals();
    // Batch 21: session-grouped ordering (A3) and LIKE guard (B4)
    void searchMessagesGroupsBySession();
    void searchMessagesLikeGuardSuppressesLargeTables();
    // Batch 22 (search-aggregate): per-session aggregation (exact hitCount,
    // latestTimestamp ordering, limit = session cap), user-preferred best
    // snippet, markdown stripping / newline flattening / [kw] markers,
    // FTS vs LIKE path consistency, archived filtering.
    void searchMessagesAggregatePerSession();
    void searchMessagesOrdersSessionsAndCapsLimit();
    void searchMessagesBestSnippetPrefersUserHit();
    void searchMessagesSnippetStripsMarkdown();
    void searchMessagesSnippetFlattensNewlines();
    void searchMessagesFtsAndLikeAgreeOnMarkers();
    void searchMessagesRespectsArchivedFilter();
    // Review fixes: M-1 (underscore identifiers survive stripping) and the
    // degradation path (keyword only inside stripped markup → marker-less
    // head window instead of a lost row).
    void searchMessagesSnippetPreservesUnderscoreIdentifiers();
    void searchMessagesSnippetDegradesWhenKeywordInStrippedMarkup();
    // docs/20 §3.1/§3.2（FORK-701）：重新生成/编辑重发的持久层支撑
};

void TestMemory::messageRoundTrip() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.saveMessage("s1", msg("user", "hello"));
    mem.saveMessage("s1", msg("assistant", "world"));

    const QJsonArray loaded = mem.loadMessages("s1", 50);
    QCOMPARE(loaded.size(), 2);
    QCOMPARE(loaded.at(0).toObject().value("role").toString(), QStringLiteral("user"));
    QCOMPARE(loaded.at(0).toObject().value("content").toString(), QStringLiteral("hello"));
    QCOMPARE(loaded.at(1).toObject().value("content").toString(), QStringLiteral("world"));
    QCOMPARE(mem.loadMessages("other-session", 50).size(), 0);
}

// A limit smaller than the stored history must keep the NEWEST messages
// (in chronological order), not the oldest ones.
void TestMemory::loadMessagesKeepsNewest() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    for (int i = 0; i < 5; ++i)
        mem.saveMessage("s1", msg("user", QStringLiteral("m%1").arg(i)));

    const QJsonArray loaded = mem.loadMessages("s1", 3);
    QCOMPARE(loaded.size(), 3);
    QCOMPARE(loaded.at(0).toObject().value("content").toString(), QStringLiteral("m2"));
    QCOMPARE(loaded.at(1).toObject().value("content").toString(), QStringLiteral("m3"));
    QCOMPARE(loaded.at(2).toObject().value("content").toString(), QStringLiteral("m4"));
}

void TestMemory::sessionBookkeepingAndClear() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("s1", "First session");
    mem.saveMessage("s1", msg("user", "hi"));
    mem.touchSession("s1");

    const QJsonArray sessions = mem.listSessions(10);
    QCOMPARE(sessions.size(), 1);
    QCOMPARE(sessions.at(0).toObject().value("id").toString(), QStringLiteral("s1"));
    QCOMPARE(sessions.at(0).toObject().value("title").toString(), QStringLiteral("First session"));
    QVERIFY(sessions.at(0).toObject().contains("timeText"));

    mem.clear("s1");
    QCOMPARE(mem.loadMessages("s1", 50).size(), 0);
}

void TestMemory::sessionDeleteAndRename() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("s1", "First");
    mem.beginSession("s2", "Second");
    mem.saveMessage("s1", msg("user", "in s1"));
    mem.saveMessage("s2", msg("user", "in s2"));

    mem.renameSession("s1", "Renamed");
    QJsonArray sessions = mem.listSessions(10);
    QCOMPARE(sessions.size(), 2);
    bool found = false;
    for (const auto& entry : sessions) {
        if (entry.toObject().value("id").toString() == "s1") {
            QCOMPARE(entry.toObject().value("title").toString(), QStringLiteral("Renamed"));
            found = true;
        }
    }
    QVERIFY(found);

    mem.deleteSession("s1");
    sessions = mem.listSessions(10);
    QCOMPARE(sessions.size(), 1);
    QCOMPARE(sessions.at(0).toObject().value("id").toString(), QStringLiteral("s2"));
    // docs/23 G7: 软删除——消息为撤销窗口保留（硬级联见
    // deletedSessionRoundTrip 的 purge 断言）。
    QVERIFY(mem.loadMessages("s1", 10).size() > 0);
    QVERIFY(mem.restoreSession("s1"));
    QCOMPARE(mem.listSessions(10).size(), 2);
}

void TestMemory::auditTrail() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.logAudit("s1", "tool_execute", "read_file", "{\"path\":\"a.txt\"}",
                 true, "File written successfully: 3 bytes");
    mem.logAudit("s1", "tool_declined", "shell_exec", "{\"command\":\"rm\"}",
                 false, "user declined execution");

    const QJsonArray entries = mem.auditLog(10);
    QCOMPARE(entries.size(), 2);

    // Newest first
    QCOMPARE(entries.at(0).toObject().value("event").toString(), QStringLiteral("tool_declined"));
    QCOMPARE(entries.at(0).toObject().value("toolName").toString(), QStringLiteral("shell_exec"));
    QVERIFY(!entries.at(0).toObject().value("success").toBool());

    QCOMPARE(entries.at(1).toObject().value("event").toString(), QStringLiteral("tool_execute"));
    QVERIFY(entries.at(1).toObject().value("success").toBool());
    QVERIFY(entries.at(1).toObject().contains("timestamp"));

    QCOMPARE(mem.auditLog(1).size(), 1);
}

// M1 (stats): tokenStats aggregates task_complete rows by model / day and
// ignores llm_request rows (their token count is 0 - unknown at request start).
void TestMemory::tokenStatsAggregate() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.logAudit("s1", "llm_request", "", "", true, "", 1, 100, 0, "gpt-4o-mini");
    mem.logAudit("s1", "task_complete", "", "", true, "done", 1, 500, 120, "gpt-4o-mini");
    mem.logAudit("s2", "task_complete", "", "", true, "done", 2, 300, 800, "qwen2.5");
    mem.logAudit("s2", "task_complete", "", "", true, "done", 3, 200, 700, "qwen2.5");

    const QVariantMap stats = mem.tokenStats(30);
    QCOMPARE(stats.value("totalCalls").toInt(), 3);
    QCOMPARE(stats.value("totalTokens").toInt(), 120 + 800 + 700);

    const QVariantList byModel = stats.value("byModel").toList();
    QCOMPARE(byModel.size(), 2);
    // ordered by total tokens DESC: qwen2.5 (1500) then gpt-4o-mini (120)
    QCOMPARE(byModel.at(0).toMap().value("model").toString(), QStringLiteral("qwen2.5"));
    QCOMPARE(byModel.at(0).toMap().value("tokens").toInt(), 1500);
    QCOMPARE(byModel.at(1).toMap().value("model").toString(), QStringLiteral("gpt-4o-mini"));
    QCOMPARE(byModel.at(1).toMap().value("tokens").toInt(), 120);

    const QVariantList byDay = stats.value("byDay").toList();
    QVERIFY(!byDay.isEmpty());
    QVERIFY(byDay.at(0).toMap().contains("day"));
    QCOMPARE(byDay.at(0).toMap().value("tokens").toInt(), 120 + 800 + 700);
}

void TestMemory::tokenStatsEmptyWhenNoData() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    const QVariantMap stats = mem.tokenStats(30);
    QCOMPARE(stats.value("totalCalls").toInt(), 0);
    QCOMPARE(stats.value("totalTokens").toInt(), 0);
    QVERIFY(stats.value("byModel").toList().isEmpty());
    QVERIFY(stats.value("byDay").toList().isEmpty());
}

// docs/17 A0: 项目 CRUD、会话归属与删除项目时的会话保留语义。
void TestMemory::projectSessionLifecycle() {
    SqliteMemory mem(QStringLiteral(":memory:"));

    const QString pid = mem.createProject(QStringLiteral("demo"),
                                          QStringLiteral("D:\\work\\demo"));
    QVERIFY(!pid.isEmpty());

    QVariantList projects = mem.listProjects();
    QCOMPARE(projects.size(), 1);
    QCOMPARE(projects.first().toMap().value("name").toString(),
             QStringLiteral("demo"));
    QCOMPARE(projects.first().toMap().value("rootPath").toString(),
             QStringLiteral("D:\\work\\demo"));

    // 会话归属：标记后 listSessions 携带 projectId。
    mem.beginSession("s1", "project session");
    mem.setSessionProject("s1", pid);
    mem.beginSession("s2", "local session");
    QJsonArray sessions = mem.listSessions(10);
    QCOMPARE(sessions.size(), 2);
    for (const auto& entry : sessions) {
        const QJsonObject s = entry.toObject();
        if (s.value("id").toString() == QLatin1String("s1"))
            QCOMPARE(s.value("projectId").toString(), pid);
        if (s.value("id").toString() == QLatin1String("s2"))
            QCOMPARE(s.value("projectId").toString(), QString());
    }

    // 脱离项目（空串 → NULL）。
    mem.setSessionProject("s1", QString());
    sessions = mem.listSessions(10);
    for (const auto& entry : sessions) {
        const QJsonObject s = entry.toObject();
        if (s.value("id").toString() == QLatin1String("s1"))
            QVERIFY(s.value("projectId").toString().isEmpty());
    }

    // 删除项目：项目记录消失，其会话保留（已脱离，仍存在）。
    mem.setSessionProject("s2", pid);
    mem.removeProject(pid);
    QVERIFY(mem.listProjects().isEmpty());
    sessions = mem.listSessions(10);
    QCOMPARE(sessions.size(), 2);   // 两个会话都还在
    for (const auto& entry : sessions)
        QVERIFY(entry.toObject().value("projectId").toString().isEmpty());
}

// docs/17 P2: renameProject 只改显示名，root_path 与会话归属不动。
void TestMemory::projectRename() {
    SqliteMemory mem(QStringLiteral(":memory:"));

    const QString pid = mem.createProject(QStringLiteral("demo"),
                                          QStringLiteral("D:/work/demo"));
    QVERIFY(!pid.isEmpty());

    mem.renameProject(pid, QStringLiteral("renamed demo"));
    QVariantList projects = mem.listProjects();
    QCOMPARE(projects.size(), 1);
    QCOMPARE(projects.first().toMap().value("name").toString(),
             QStringLiteral("renamed demo"));
    QCOMPARE(projects.first().toMap().value("rootPath").toString(),
             QStringLiteral("D:/work/demo"));

    // 未知 id：静默无操作，列表不变。
    mem.renameProject(QStringLiteral("no-such-id"), QStringLiteral("ghost"));
    QCOMPARE(mem.listProjects().first().toMap().value("name").toString(),
             QStringLiteral("renamed demo"));
}

// docs/16 §8.3: sessionTokenBreakdown aggregates one session's token_usage
// rows per direction/source; other sessions and empty sessions stay out.
void TestMemory::sessionTokenBreakdown() {
    SqliteMemory mem(QStringLiteral(":memory:"));

    // 7 input sources sum to 100; 3 output sources to 50; cache hits to 40.
    QVariantMap rec;
    rec["sessionId"] = "s1";
    rec["model"] = "gpt-4o";
    rec["requestId"] = 1;
    rec["inputTokens"] = 100;
    rec["outputTokens"] = 50;
    rec["cacheHitTokens"] = 40;
    rec["inputSystemPrompt"] = 30;
    rec["inputConversation"] = 20;
    rec["inputToolCall"] = 10;
    rec["inputMcp"] = 15;
    rec["inputSkill"] = 5;
    rec["inputImage"] = 12;
    rec["inputOther"] = 8;
    rec["outputConversation"] = 20;
    rec["outputToolCall"] = 15;
    rec["outputOther"] = 15;
    rec["cacheHitSystemPrompt"] = 25;
    rec["cacheHitConversation"] = 10;
    rec["cacheHitToolCall"] = 5;
    rec["costCents"] = 7;
    mem.logTokenUsage(rec);

    // A second session must not leak into s1's breakdown.
    QVariantMap other = rec;
    other["sessionId"] = "s2";
    other["inputTokens"] = 1000;
    other["outputTokens"] = 1000;
    other["costCents"] = 999;
    mem.logTokenUsage(other);

    const QVariantMap b = mem.sessionTokenBreakdown("s1");
    QCOMPARE(b.value("totalCalls").toInt(), 1);
    QCOMPARE(b.value("totalInputTokens").toLongLong(), 100);
    QCOMPARE(b.value("totalOutputTokens").toLongLong(), 50);
    QCOMPARE(b.value("totalCacheHitTokens").toLongLong(), 40);
    QCOMPARE(b.value("totalTokens").toLongLong(), 150);
    QCOMPARE(b.value("totalCostCents").toLongLong(), 7);
    QCOMPARE(b.value("inputSystemPrompt").toLongLong(), 30);
    QCOMPARE(b.value("inputMcp").toLongLong(), 15);
    QCOMPARE(b.value("inputSkill").toLongLong(), 5);
    QCOMPARE(b.value("inputImage").toLongLong(), 12);
    QCOMPARE(b.value("inputOther").toLongLong(), 8);
    QCOMPARE(b.value("outputConversation").toLongLong(), 20);
    QCOMPARE(b.value("outputToolCall").toLongLong(), 15);
    QCOMPARE(b.value("outputOther").toLongLong(), 15);
    QCOMPARE(b.value("outputMcp").toLongLong(), 0);
    QCOMPARE(b.value("outputSkill").toLongLong(), 0);
    // cacheHitRate = 40 / 100
    QVERIFY(qFuzzyCompare(b.value("cacheHitRate").toDouble(), 0.4));

    // Session with no requests: empty aggregate (UI shows the empty state).
    const QVariantMap empty = mem.sessionTokenBreakdown("s-none");
    QCOMPARE(empty.value("totalCalls").toInt(), 0);
    QCOMPARE(empty.value("totalTokens").toLongLong(), 0);
    QCOMPARE(empty.value("cacheHitRate").toDouble(), 0.0);

    // Blank id: defensive no-op rather than a full-table scan.
    QVERIFY(mem.sessionTokenBreakdown(QString()).isEmpty());
}

// Batch 6 T1: tokenStatsDetailed 起止时间戳范围模式。「今日」按本地自然日
// 0 点起算（QDateTime 口径对齐写入侧）、fromTs==toTs 空范围、byModel/byDay
// 在自定义范围内的分组正确性；fromTs/toTs 均 0 时回退既有 recentDays 窗口
// （兼容回归）。所有窗口/时间戳均由同一 QDate 派生，断言自洽不受运行时钟
// 跨午夜影响。
void TestMemory::tokenStatsDetailedCustomRange() {
    SqliteMemory mem(QStringLiteral(":memory:"));

    // 本地自然日 0 点口径（与 quotaStatus / 写入侧 QDateTime 一致）。
    const QDate today = QDate::currentDate();
    const qint64 todayStart =
        QDateTime(today, QTime(0, 0)).toMSecsSinceEpoch();
    const qint64 tomorrowStart =
        QDateTime(today.addDays(1), QTime(0, 0)).toMSecsSinceEpoch();
    const qint64 yesterdayStart =
        QDateTime(today.addDays(-1), QTime(0, 0)).toMSecsSinceEpoch();
    const qint64 yesterdayLate =
        QDateTime(today.addDays(-1), QTime(23, 59, 59, 999)).toMSecsSinceEpoch();

    // 今天两条（gpt-4o / qwen2.5）+ 昨天一条（gpt-4o，23:59:59.999）。
    mem.logTokenUsage(usageRec("gpt-4o", todayStart + 1, 100, 50, 40, 7));
    mem.logTokenUsage(usageRec("qwen2.5", todayStart + 3600000, 200, 100, 0, 11));
    mem.logTokenUsage(usageRec("gpt-4o", yesterdayLate, 1000, 500, 0, 70));

    // 「今日」[今天0点, 明天0点)：只含今天两条，昨天 23:59:59.999 被排除。
    const QVariantMap todayStats =
        mem.tokenStatsDetailed(0, todayStart, tomorrowStart);
    QCOMPARE(todayStats.value("totalCalls").toInt(), 2);
    QCOMPARE(todayStats.value("totalInputTokens").toLongLong(), 300);
    QCOMPARE(todayStats.value("totalOutputTokens").toLongLong(), 150);
    QCOMPARE(todayStats.value("totalCacheHitTokens").toLongLong(), 40);
    QCOMPARE(todayStats.value("totalCostCents").toLongLong(), 18);

    // byModel 范围内分组：qwen2.5 总量 300 > gpt-4o 今日 150（DESC 在前），
    // 昨天那条 gpt-4o 的 1000 input 不得泄漏进分组。
    const QVariantList byModel = todayStats.value("byModel").toList();
    QCOMPARE(byModel.size(), 2);
    QCOMPARE(byModel.at(0).toMap().value("model").toString(),
             QStringLiteral("qwen2.5"));
    QCOMPARE(byModel.at(0).toMap().value("calls").toInt(), 1);
    QCOMPARE(byModel.at(0).toMap().value("inputTokens").toLongLong(), 200);
    QCOMPARE(byModel.at(1).toMap().value("model").toString(),
             QStringLiteral("gpt-4o"));
    QCOMPARE(byModel.at(1).toMap().value("inputTokens").toLongLong(), 100);

    // byDay 范围内分组：今日窗口只剩今天一组。
    const QVariantList byDay = todayStats.value("byDay").toList();
    QCOMPARE(byDay.size(), 1);
    QCOMPARE(byDay.at(0).toMap().value("day").toString(),
             today.toString(Qt::ISODate));
    QCOMPARE(byDay.at(0).toMap().value("inputTokens").toLongLong(), 300);
    QCOMPARE(byDay.at(0).toMap().value("calls").toInt(), 2);

    // 跨两天窗口 [昨天0点, 明天0点)：byDay 两组按天独立聚合，DESC 最新在前。
    const QVariantMap bothDays =
        mem.tokenStatsDetailed(0, yesterdayStart, tomorrowStart);
    QCOMPARE(bothDays.value("totalCalls").toInt(), 3);
    const QVariantList days = bothDays.value("byDay").toList();
    QCOMPARE(days.size(), 2);
    QCOMPARE(days.at(0).toMap().value("day").toString(),
             today.toString(Qt::ISODate));
    QCOMPARE(days.at(0).toMap().value("inputTokens").toLongLong(), 300);
    QCOMPARE(days.at(1).toMap().value("day").toString(),
             today.addDays(-1).toString(Qt::ISODate));
    QCOMPARE(days.at(1).toMap().value("inputTokens").toLongLong(), 1000);
    QCOMPARE(days.at(1).toMap().value("calls").toInt(), 1);

    // fromTs == toTs：左闭右开语义下必为空范围。
    const QVariantMap empty =
        mem.tokenStatsDetailed(0, todayStart, todayStart);
    QCOMPARE(empty.value("totalCalls").toInt(), 0);
    QCOMPARE(empty.value("totalInputTokens").toLongLong(), 0);
    QVERIFY(empty.value("byModel").toList().isEmpty());
    QVERIFY(empty.value("byDay").toList().isEmpty());

    // 只有下界（toTs=0 不设上界）：从昨天 0 点起全部 3 条。
    const QVariantMap sinceYesterday =
        mem.tokenStatsDetailed(0, yesterdayStart, 0);
    QCOMPARE(sinceYesterday.value("totalCalls").toInt(), 3);

    // 兼容回归：fromTs/toTs 均 0 → 回退 recentDays 窗口（30 天全包含）。
    const QVariantMap fallback = mem.tokenStatsDetailed(30);
    QCOMPARE(fallback.value("totalCalls").toInt(), 3);
}

// Batch 6 T1: tokenUsageRecords 分页明细。timestamp DESC（同刻按 id DESC
// 稳定）、limit/offset 分页遍历不重不漏、offset 越界返回空 rows 但 total
// 正确、范围过滤（左闭右开）、fromTs==toTs 空范围、limit<=0 取默认 50、
// 行字段完整（驼峰九键）。
void TestMemory::tokenUsageRecordsPagination() {
    SqliteMemory mem(QStringLiteral(":memory:"));

    // 7 条记录，时间严格递增（ts_i = base + i * 1000）。
    const qint64 base = 1700000000000LL;
    for (int i = 0; i < 7; ++i) {
        mem.logTokenUsage(usageRec(i % 2 == 0 ? "gpt-4o" : "qwen2.5",
                                   base + i * 1000,
                                   10 + i, 20 + i, 5, 3 + i, 100 + i));
    }

    // 全量一页（limit 50）：7 行，timestamp 严格递减。
    const QVariantMap all = mem.tokenUsageRecords(0, 0, 50, 0);
    QCOMPARE(all.value("total").toInt(), 7);
    const QVariantList rows = all.value("rows").toList();
    QCOMPARE(rows.size(), 7);
    for (int i = 1; i < rows.size(); ++i) {
        QVERIFY(rows.at(i).toMap().value("timestamp").toLongLong()
                < rows.at(i - 1).toMap().value("timestamp").toLongLong());
    }

    // 行字段完整（驼峰九键）与值：最新一行是 i=6（base + 6000）。
    const QVariantMap first = rows.at(0).toMap();
    QCOMPARE(first.value("requestId").toLongLong(), 42);
    QCOMPARE(first.value("sessionId").toString(), QStringLiteral("s-range"));
    QCOMPARE(first.value("model").toString(), QStringLiteral("gpt-4o"));
    QCOMPARE(first.value("timestamp").toLongLong(), base + 6000);
    QCOMPARE(first.value("inputTokens").toLongLong(), 16);
    QCOMPARE(first.value("outputTokens").toLongLong(), 26);
    QCOMPARE(first.value("cacheHitTokens").toLongLong(), 5);
    QCOMPARE(first.value("latencyMs").toLongLong(), 106);
    QCOMPARE(first.value("costCents").toLongLong(), 9);

    // 分页遍历：limit 3 → 3 + 3 + 1，翻页间不重不漏且次序稳定。
    const QVariantMap p1 = mem.tokenUsageRecords(0, 0, 3, 0);
    const QVariantMap p2 = mem.tokenUsageRecords(0, 0, 3, 3);
    const QVariantMap p3 = mem.tokenUsageRecords(0, 0, 3, 6);
    QCOMPARE(p1.value("total").toInt(), 7);
    QCOMPARE(p2.value("total").toInt(), 7);
    QCOMPARE(p3.value("total").toInt(), 7);
    QCOMPARE(p1.value("rows").toList().size(), 3);
    QCOMPARE(p2.value("rows").toList().size(), 3);
    QCOMPARE(p3.value("rows").toList().size(), 1);
    QCOMPARE(p1.value("rows").toList().at(0).toMap().value("timestamp").toLongLong(),
             base + 6000);
    QCOMPARE(p2.value("rows").toList().at(0).toMap().value("timestamp").toLongLong(),
             base + 3000);
    QCOMPARE(p3.value("rows").toList().at(0).toMap().value("timestamp").toLongLong(),
             base + 0);
    QVERIFY(p1.value("rows").toList().at(2).toMap().value("timestamp").toLongLong()
            > p2.value("rows").toList().at(0).toMap().value("timestamp").toLongLong());

    // offset 越界：rows 为空但 total 仍正确。
    const QVariantMap over = mem.tokenUsageRecords(0, 0, 50, 100);
    QCOMPARE(over.value("total").toInt(), 7);
    QVERIFY(over.value("rows").toList().isEmpty());

    // 范围过滤 [base+2000, base+5000)：左闭右开 → 恰含 2000/3000/4000。
    const QVariantMap ranged =
        mem.tokenUsageRecords(base + 2000, base + 5000, 50, 0);
    QCOMPARE(ranged.value("total").toInt(), 3);
    QCOMPARE(ranged.value("rows").toList().size(), 3);
    QCOMPARE(ranged.value("rows").toList().at(0).toMap().value("timestamp").toLongLong(),
             base + 4000);

    // fromTs == toTs：空范围。
    const QVariantMap emptyRange =
        mem.tokenUsageRecords(base + 2000, base + 2000, 50, 0);
    QCOMPARE(emptyRange.value("total").toInt(), 0);
    QVERIFY(emptyRange.value("rows").toList().isEmpty());

    // limit <= 0 → 默认每页 50。
    const QVariantMap defLimit = mem.tokenUsageRecords(0, 0, -5, 0);
    QCOMPARE(defLimit.value("rows").toList().size(), 7);
}

// Batch 6 T1: 明细与聚合口径一致——同范围下 records.total ==
// detailed.totalCalls，SUM(明细各列) == detailed 对应聚合值（跨页合计）；
// 范围外记录两边都不计入。
void TestMemory::tokenUsageRecordsMatchAggregates() {
    SqliteMemory mem(QStringLiteral(":memory:"));

    const QDate today = QDate::currentDate();
    const qint64 todayStart =
        QDateTime(today, QTime(0, 0)).toMSecsSinceEpoch();
    const qint64 tomorrowStart =
        QDateTime(today.addDays(1), QTime(0, 0)).toMSecsSinceEpoch();

    // 范围内 3 条 + 范围外 1 条（昨天 23:59:59.999，今日窗口之外）。
    mem.logTokenUsage(usageRec("gpt-4o", todayStart + 1000, 100, 50, 40, 7));
    mem.logTokenUsage(usageRec("qwen2.5", todayStart + 2000, 200, 80, 60, 9));
    mem.logTokenUsage(usageRec("gpt-4o", todayStart + 3000, 30, 20, 10, 2));
    mem.logTokenUsage(usageRec("claude",
                               QDateTime(today.addDays(-1), QTime(23, 59, 59, 999))
                                   .toMSecsSinceEpoch(),
                               999, 999, 0, 999));

    const QVariantMap detailed =
        mem.tokenStatsDetailed(0, todayStart, tomorrowStart);
    // limit 2 强制跨页取全（3 = 2 + 1），顺带覆盖跨页合计。
    const QVariantMap page1 =
        mem.tokenUsageRecords(todayStart, tomorrowStart, 2, 0);
    const QVariantMap page2 =
        mem.tokenUsageRecords(todayStart, tomorrowStart, 2, 2);

    QCOMPARE(detailed.value("totalCalls").toInt(), 3);
    QCOMPARE(page1.value("total").toInt(), detailed.value("totalCalls").toInt());

    QVariantList rows = page1.value("rows").toList();
    rows += page2.value("rows").toList();
    QCOMPARE(rows.size(), 3);

    qint64 sumIn = 0, sumOut = 0, sumCache = 0, sumCost = 0;
    for (const auto& rowV : rows) {
        const QVariantMap row = rowV.toMap();
        sumIn += row.value("inputTokens").toLongLong();
        sumOut += row.value("outputTokens").toLongLong();
        sumCache += row.value("cacheHitTokens").toLongLong();
        sumCost += row.value("costCents").toLongLong();
    }
    QCOMPARE(sumIn, detailed.value("totalInputTokens").toLongLong());
    QCOMPARE(sumOut, detailed.value("totalOutputTokens").toLongLong());
    QCOMPARE(sumCache, detailed.value("totalCacheHitTokens").toLongLong());
    QCOMPARE(sumCost, detailed.value("totalCostCents").toLongLong());
    // 范围外 claude 行（999 input/999 cents）不进任何一边。
    QCOMPARE(sumIn, qint64(330));
    QCOMPARE(sumCost, qint64(18));
}

// Batch 6 T1: costStats / tokenStatsBySource 范围模式——范围外记录被排除；
// fromTs/toTs 均 0 回退 recentDays 窗口（兼容回归）。
void TestMemory::costStatsAndBySourceCustomRange() {
    SqliteMemory mem(QStringLiteral(":memory:"));

    const QDate today = QDate::currentDate();
    const qint64 todayStart =
        QDateTime(today, QTime(0, 0)).toMSecsSinceEpoch();
    const qint64 tomorrowStart =
        QDateTime(today.addDays(1), QTime(0, 0)).toMSecsSinceEpoch();

    QVariantMap in1;
    in1["sessionId"] = "s1";
    in1["model"] = "gpt-4o";
    in1["requestId"] = 1;
    in1["timestamp"] = todayStart + 1000;
    in1["inputTokens"] = 100;
    in1["outputTokens"] = 50;
    in1["inputSystemPrompt"] = 60;
    in1["inputConversation"] = 40;
    in1["costCents"] = 7;
    QVariantMap in2;
    in2["sessionId"] = "s2";
    in2["model"] = "qwen2.5";
    in2["requestId"] = 2;
    in2["timestamp"] = todayStart + 2000;
    in2["inputTokens"] = 200;
    in2["outputTokens"] = 100;
    in2["inputSystemPrompt"] = 150;
    in2["inputConversation"] = 50;
    in2["costCents"] = 11;
    QVariantMap out1; // 昨天深夜：今日窗口之外
    out1["sessionId"] = "s3";
    out1["model"] = "gpt-4o";
    out1["requestId"] = 3;
    out1["timestamp"] =
        QDateTime(today.addDays(-1), QTime(23, 59, 59, 999)).toMSecsSinceEpoch();
    out1["inputTokens"] = 1000;
    out1["outputTokens"] = 500;
    out1["inputSystemPrompt"] = 900;
    out1["costCents"] = 700;
    mem.logTokenUsage(in1);
    mem.logTokenUsage(in2);
    mem.logTokenUsage(out1);

    // costStats 范围模式：只累计今日两条（7 + 11 = 18 分），昨天的
    // 大额 gpt-4o（700 分）不泄漏进 totalCostCents / byModel / byDay。
    const QVariantMap cost = mem.costStats(0, todayStart, tomorrowStart);
    QCOMPARE(cost.value("totalCostCents").toLongLong(), 18);
    QCOMPARE(cost.value("byModel").toList().size(), 2);
    QCOMPARE(cost.value("byDay").toList().size(), 1);

    // tokenStatsBySource 范围模式：来源固定 7 行，system_prompt 只累计
    // 今日（60 + 150 = 210），昨天的 900 不泄漏。
    const QVariantList sources =
        mem.tokenStatsBySource(0, todayStart, tomorrowStart);
    QCOMPARE(sources.size(), 7);
    const QVariantMap sp = sources.at(0).toMap();
    QCOMPARE(sp.value("source").toString(), QStringLiteral("system_prompt"));
    QCOMPARE(sp.value("inputTokens").toLongLong(), 210);
    const QVariantMap conv = sources.at(1).toMap();
    QCOMPARE(conv.value("source").toString(), QStringLiteral("conversation"));
    QCOMPARE(conv.value("inputTokens").toLongLong(), 90);

    // 兼容回归：fromTs/toTs 均 0 → 回退 recentDays（30 天全包含）。
    const QVariantMap costAll = mem.costStats(30);
    QCOMPARE(costAll.value("totalCostCents").toLongLong(), 718);
    const QVariantList sourcesAll = mem.tokenStatsBySource(30);
    QCOMPARE(sourcesAll.at(0).toMap().value("inputTokens").toLongLong(), 1110);
}

// 批次六审查 M1: byDay 行上限语义。连续 40 天数据：
// - 范围模式（fromTs/toTs 任一 > 0）：tokenStatsDetailed / costStats 的
//   byDay 必须完整返回 40 行（不被旧 LIMIT 31 静默截断为最近 31 天，
//   与同页环形图/柱状图全量口径一致）；
// - recentDays 回退模式（fromTs/toTs 均 0）：byDay 仍按既有 31 行上限
//   截断最近 31 天（回归锁定，防止回退窗口被一并放宽）。
// 所有时间戳由同一 QDate 派生，断言不受运行时钟跨午夜影响。
void TestMemory::byDayRangeModeLimitSemantics() {
    SqliteMemory mem(QStringLiteral(":memory:"));

    // 连续 40 天（today-39 ~ today），每天一条记录（12:00）。
    const QDate today = QDate::currentDate();
    const qint64 tomorrowStart =
        QDateTime(today.addDays(1), QTime(0, 0)).toMSecsSinceEpoch();
    const qint64 rangeFrom =
        QDateTime(today.addDays(-39), QTime(0, 0)).toMSecsSinceEpoch();
    for (int i = 0; i < 40; ++i) {
        mem.logTokenUsage(usageRec("gpt-4o",
            QDateTime(today.addDays(-39 + i), QTime(12, 0))
                .toMSecsSinceEpoch(),
            10 + i, 5, 0, 1, 100));
    }

    // 范围模式 [39 天前 0 点, 明天 0 点)：覆盖全部 40 天 → byDay 完整
    // 40 行（DESC 最新在前：首行今天、末行 39 天前）。
    const QVariantMap stats = mem.tokenStatsDetailed(0, rangeFrom, tomorrowStart);
    QCOMPARE(stats.value("totalCalls").toInt(), 40);
    const QVariantList byDay = stats.value("byDay").toList();
    QCOMPARE(byDay.size(), 40);
    QCOMPARE(byDay.at(0).toMap().value("day").toString(),
             today.toString(Qt::ISODate));
    QCOMPARE(byDay.at(39).toMap().value("day").toString(),
             today.addDays(-39).toString(Qt::ISODate));

    // costStats 同一范围：byDay 同样 40 行（两处查询共用上限语义）。
    const QVariantMap cost = mem.costStats(0, rangeFrom, tomorrowStart);
    QCOMPARE(cost.value("byDay").toList().size(), 40);

    // 回退模式（fromTs/toTs 均 0）：recentDays=45 窗口全包含 40 天，
    // 但 byDay 仍按 31 行上限截断最近 31 天（today ~ today-30）。
    const QVariantMap fallback = mem.tokenStatsDetailed(45);
    const QVariantList fallbackDays = fallback.value("byDay").toList();
    QCOMPARE(fallbackDays.size(), 31);
    QCOMPARE(fallbackDays.at(0).toMap().value("day").toString(),
             today.toString(Qt::ISODate));
    QCOMPARE(fallbackDays.at(30).toMap().value("day").toString(),
             today.addDays(-30).toString(Qt::ISODate));
}

void TestMemory::episodicFullTextSearch() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    QJsonObject episode;
    episode["summary"] = "The launch checklist lives in docs/launch.md";
    episode["tags"] = "docs";
    mem.saveEpisodic("s1", episode);

    const QJsonArray hits = mem.searchEpisodic("launch checklist", 5);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.at(0).toObject().value("summary").toString(),
             QStringLiteral("The launch checklist lives in docs/launch.md"));
    QCOMPARE(mem.searchEpisodic("zzz_no_match", 5).size(), 0);
}

// Trace nodes: upsert semantics (position fixed on first insert, content
// refreshes afterwards) and per-session cleanup.
void TestMemory::traceNodeRoundTrip() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    // docs/23 G7: a real session row exists in production, and the purge
    // cascade below only targets sessions that were soft-deleted.
    mem.beginSession("s1", "Trace");

    QJsonObject node;
    node["id"] = QStringLiteral("n1");
    node["type"] = QStringLiteral("tool_call");
    node["title"] = QStringLiteral("read_file");
    node["status"] = QStringLiteral("running");
    node["args"] = QStringLiteral("{\"path\":\"a.txt\"}");
    node["result"] = QString();
    node["timestamp"] = 1000.0;
    mem.saveTraceNode("s1", node, 0);

    QJsonObject node2 = node;
    node2["id"] = QStringLiteral("n2");
    node2["type"] = QStringLiteral("summary");
    node2["title"] = QStringLiteral("Final answer");
    node2["status"] = QStringLiteral("completed");
    node2["args"] = QString();
    node2["timestamp"] = 2000.0;
    mem.saveTraceNode("s1", node2, 1);

    // Status update for n1: same id -> upsert, original position kept
    node["status"] = QStringLiteral("success");
    node["result"] = QStringLiteral("file body");
    node["timestamp"] = 3000.0;
    mem.saveTraceNode("s1", node, 7);

    const QJsonArray loaded = mem.loadTraceNodes("s1");
    QCOMPARE(loaded.size(), 2);
    QCOMPARE(loaded.at(0).toObject().value("id").toString(), QStringLiteral("n1"));
    QCOMPARE(loaded.at(0).toObject().value("order").toInt(), 0); // first insert wins
    QCOMPARE(loaded.at(0).toObject().value("status").toString(), QStringLiteral("success"));
    QCOMPARE(loaded.at(0).toObject().value("result").toString(), QStringLiteral("file body"));
    QCOMPARE(loaded.at(1).toObject().value("id").toString(), QStringLiteral("n2"));
    QCOMPARE(loaded.at(1).toObject().value("title").toString(), QStringLiteral("Final answer"));

    QVERIFY(mem.loadTraceNodes("other").isEmpty());
    mem.deleteSession("s1");
    // docs/23 G7: 软删除保留轨迹供撤销；硬级联移至 purgeDeletedSessions。
    QVERIFY(!mem.loadTraceNodes("s1").isEmpty());
    QCOMPARE(mem.purgeDeletedSessions(
                 QDateTime::currentMSecsSinceEpoch() + 1000), 1);
    QVERIFY(mem.loadTraceNodes("s1").isEmpty());
}


// ── Batch 6: knowledge persistence v2 ─────────────────────

// A v1 database (documents without hash/model columns, no chunk table)
// must open cleanly: the migration adds the columns, keeps the rows, and
// marks them stale (empty hash) so the first startup re-embeds them.
void TestMemory::knowledgeSchemaV1Migrates() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                        + "/nexusai-memtest-" + QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(dir);
    const QString dbPath = dir + "/v1.db";
    QFile::remove(dbPath);

    {
        // Build a v1-schema database by hand
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "v1raw");
        db.setDatabaseName(dbPath);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE knowledge_documents (name TEXT PRIMARY KEY, "
                       "content TEXT NOT NULL, imported_at INTEGER NOT NULL)"));
        q.prepare("INSERT INTO knowledge_documents VALUES (?, ?, ?)");
        q.addBindValue("old.txt");
        q.addBindValue("legacy content");
        q.addBindValue(123);
        QVERIFY(q.exec());
        db.close();
    }
    QSqlDatabase::removeDatabase("v1raw");

    SqliteMemory memory(dbPath);
    const auto records = memory.loadKnowledgeDocRecords();
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.at(0).name, QStringLiteral("old.txt"));
    QCOMPARE(records.at(0).content, QStringLiteral("legacy content"));
    // Stale markers: migration must NOT fabricate a hash/model
    QVERIFY(records.at(0).contentHash.isEmpty());
    QVERIFY(records.at(0).embeddingModel.isEmpty());
    QVERIFY(memory.loadKnowledgeChunks("old.txt").isEmpty());

    QFile::remove(dbPath);
    QDir(dir).removeRecursively();
}

// Chunk round trip: upsert + replaceKnowledgeChunks + load, then a stale
// record set through a model change is visible on the record API.
void TestMemory::knowledgeChunkPersistence() {
    SqliteMemory memory(":memory:");

    QVector<KnowledgeChunk> chunks;
    for (int i = 0; i < 3; ++i) {
        KnowledgeChunk c;
        c.index = i;
        c.total = 3;
        c.text = QStringLiteral("chunk %1 text").arg(i);
        c.embedding = QVector<float>{1.0f * i, 0.5f, -2.0f};
        c.dim = 3;
        c.pseudo = false;
        chunks.append(c);
    }

    memory.upsertKnowledgeDoc("doc.txt", "full content",
                              knowledgeContentHash("full content"), "m1");
    memory.replaceKnowledgeChunks("doc.txt", chunks);

    QCOMPARE(memory.loadKnowledgeChunks("doc.txt").size(), 3);
    const auto loaded = memory.loadKnowledgeChunks("doc.txt").at(2);
    QCOMPARE(loaded.index, 2);
    QCOMPARE(loaded.total, 3);
    QCOMPARE(loaded.text, QStringLiteral("chunk 2 text"));
    QCOMPARE(loaded.dim, 3);
    QCOMPARE(loaded.pseudo, false);
    QVERIFY(loaded.embedding.size() == 3);
    QCOMPARE(loaded.embedding.at(2), -2.0f);

    // Replacement removes the previous rows
    QVector<KnowledgeChunk> one;
    KnowledgeChunk c;
    c.index = 0; c.total = 1; c.text = "replaced";
    c.embedding = QVector<float>{9.0f}; c.dim = 1; c.pseudo = false;
    one.append(c);
    memory.replaceKnowledgeChunks("doc.txt", one);
    QCOMPARE(memory.loadKnowledgeChunks("doc.txt").size(), 1);
    QCOMPARE(memory.loadKnowledgeChunks("doc.txt").at(0).text, QStringLiteral("replaced"));

    // Model identity is exposed for staleness decisions
    const auto records = memory.loadKnowledgeDocRecords();
    QCOMPARE(records.size(), 1);
    QCOMPARE(records.at(0).embeddingModel, QStringLiteral("m1"));
    QCOMPARE(records.at(0).contentHash, knowledgeContentHash(QStringLiteral("full content")));
}

// Removing/clearing documents cascades to their chunks - no orphan vectors
// may survive (a deleted document must not stay retrievable).
void TestMemory::knowledgeCascadeDelete() {
    SqliteMemory memory(":memory:");
    QVector<KnowledgeChunk> chunks;
    KnowledgeChunk c;
    c.index = 0; c.total = 1; c.text = "x";
    c.embedding = QVector<float>{1.0f}; c.dim = 1; c.pseudo = false;
    chunks.append(c);

    memory.upsertKnowledgeDoc("a.txt", "aaa",
                              knowledgeContentHash("aaa"), "m1");
    memory.upsertKnowledgeDoc("b.txt", "bbb",
                              knowledgeContentHash("bbb"), "m1");
    memory.replaceKnowledgeChunks("a.txt", chunks);
    memory.replaceKnowledgeChunks("b.txt", chunks);

    memory.removeKnowledgeDoc("a.txt");
    QVERIFY(memory.loadKnowledgeChunks("a.txt").isEmpty());
    QCOMPARE(memory.loadKnowledgeChunks("b.txt").size(), 1);

    memory.clearKnowledgeDocs();
    QVERIFY(memory.loadKnowledgeChunks("b.txt").isEmpty());
    QVERIFY(memory.loadKnowledgeDocRecords().isEmpty());
}


// ── Batch 7: Conversation/Memory consistency ──────────────

// MEM-702: the constructor registers a UUID-named QSqlDatabase connection
// but (pre-fix) never stored it in m_connName, so the destructor's
// removeDatabase never ran - every instance leaked its connection for the
// lifetime of the process.
void TestMemory::sqliteConnectionNameCleanedUp() {
    const int before = QSqlDatabase::connectionNames().size();
    {
        SqliteMemory a(":memory:");
        SqliteMemory b(":memory:");
        QCOMPARE(QSqlDatabase::connectionNames().size(), before + 2);

        QJsonObject msg;
        msg["role"] = QStringLiteral("user");
        msg["content"] = QStringLiteral("x");
        a.saveMessage(QStringLiteral("s"), msg);   // both instances usable
        b.saveMessage(QStringLiteral("s"), msg);
    }
    QCOMPARE(QSqlDatabase::connectionNames().size(), before);  // destroyed → gone
}

// MEM-704: workspace_id is a forward-compatibility reservation (default
// 'local', no multi-tenant behavior). Both paths must provide it:
// migrated v1 tables (ALTER) and fresh v2 DDL.
void TestMemory::workspaceIdColumnReserved() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                        + "/nexusai-memtest-" + QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(dir);
    const QString dbPath = dir + "/v1ws.db";
    QFile::remove(dbPath);

    {
        // v1 schema: three tables WITHOUT workspace_id
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "v1ws-raw");
        db.setDatabaseName(dbPath);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE messages (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "session_id TEXT NOT NULL, role TEXT NOT NULL, content TEXT NOT NULL, "
                       "timestamp INTEGER NOT NULL, metadata TEXT)"));
        QVERIFY(q.exec("CREATE TABLE audit_log (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "session_id TEXT NOT NULL, event TEXT NOT NULL, tool_name TEXT NOT NULL, "
                       "arguments TEXT, success INTEGER NOT NULL, detail TEXT, "
                       "timestamp INTEGER NOT NULL)"));
        QVERIFY(q.exec("CREATE TABLE trace_nodes (session_id TEXT NOT NULL, "
                       "node_id TEXT NOT NULL, node_order INTEGER NOT NULL, type TEXT NOT NULL, "
                       "title TEXT NOT NULL, status TEXT NOT NULL, args TEXT NOT NULL, "
                       "result TEXT NOT NULL, timestamp INTEGER NOT NULL, "
                       "PRIMARY KEY (session_id, node_id))"));
        db.close();
    }
    QSqlDatabase::removeDatabase("v1ws-raw");

    SqliteMemory memory(dbPath);
    QJsonObject msg;
    msg["role"] = QStringLiteral("user");
    msg["content"] = QStringLiteral("x");
    memory.saveMessage(QStringLiteral("s1"), msg);
    memory.logAudit(QStringLiteral("s1"), QStringLiteral("tool_execute"),
                    QStringLiteral("read_file"), QStringLiteral("{}"), true,
                    QStringLiteral("detail"));
    QJsonObject node;
    node["id"] = QStringLiteral("n1");
    node["type"] = QStringLiteral("thinking");
    node["title"] = QStringLiteral("t");
    node["status"] = QStringLiteral("completed");
    node["timestamp"] = 1234.0;
    node["args"] = QString();
    node["result"] = QString();
    memory.saveTraceNode(QStringLiteral("s1"), node, 0);

    // Inspect through an independent connection to the same file
    {
        QSqlDatabase check = QSqlDatabase::addDatabase("QSQLITE", "v1ws-check");
        check.setDatabaseName(dbPath);
        QVERIFY(check.open());
        QSqlQuery q(check);
        for (const auto* table : { "messages", "audit_log", "trace_nodes" }) {
            QVERIFY2(q.exec(QString("SELECT workspace_id FROM %1").arg(table)),
                     qPrintable(QString("workspace_id missing on %1").arg(table)));
            while (q.next())
                QCOMPARE(q.value(0).toString(), QStringLiteral("local"));
        }
        check.close();
    }
    QSqlDatabase::removeDatabase("v1ws-check");

    // Fresh v2 database carries the column from the start
    const QString freshPath = dir + "/v2ws.db";
    QFile::remove(freshPath);
    {
        SqliteMemory fresh(freshPath);
        fresh.saveMessage(QStringLiteral("s2"), msg);
    }
    {
        QSqlDatabase check = QSqlDatabase::addDatabase("QSQLITE", "v2ws-check");
        check.setDatabaseName(freshPath);
        QVERIFY(check.open());
        QSqlQuery q(check);
        QVERIFY(q.exec("SELECT workspace_id FROM messages"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QStringLiteral("local"));
        check.close();
    }
    QSqlDatabase::removeDatabase("v2ws-check");

    QFile::remove(dbPath);
    QFile::remove(freshPath);
    QDir(dir).removeRecursively();
}


// usage_log: add → load (newest first) → prune by row budget (newest kept).
void TestMemory::usageLogRoundTripAndPrune() {
    SqliteMemory memory(":memory:");
    for (int i = 1; i <= 5; ++i) {
        memory.addUsageEvent(QStringLiteral("nexus.user"),
                             QStringLiteral("action%1").arg(i),
                             QStringLiteral("target%1").arg(i),
                             static_cast<quint64>(i),
                             QStringLiteral("d%1").arg(i));
    }

    const auto events = memory.loadUsageEvents(10);
    QCOMPARE(events.size(), 5);
    QCOMPARE(events.at(0).toObject().value("action").toString(),
             QStringLiteral("action5"));   // newest first
    QCOMPARE(events.at(4).toObject().value("action").toString(),
             QStringLiteral("action1"));
    QCOMPARE(events.at(2).toObject().value("requestId").toDouble(), 3.0);
    QCOMPARE(events.at(2).toObject().value("category").toString(),
             QStringLiteral("nexus.user"));

    // Row-budget prune keeps the newest 3
    memory.pruneUsage(0, 3);
    QCOMPARE(memory.loadUsageEvents(10).size(), 3);
    QVERIFY(memory.loadUsageEvents(10).at(0).toObject().value("action")
                .toString() == QStringLiteral("action5"));

    // Empty detail/target must not violate the NOT NULL constraints
    // (hotfix: empty QString bound as SQL NULL silently dropped rows —
    // observed live as "Failed to write usage event" warnings)
    const int before = memory.loadUsageEvents(100).size();
    memory.addUsageEvent(QStringLiteral("nexus.network"), QStringLiteral("llm_result"),
                         QStringLiteral("http://x"), 9, QString());
    QCOMPARE(memory.loadUsageEvents(100).size(), before + 1);
    QVERIFY(memory.loadUsageEvents(100).at(0).toObject().value("detail")
                .toString().isEmpty());
}

// AgentViewModel holds an IMemory*, so the usage timeline must come back
// through the interface pointer: if SqliteMemory::loadUsageEvents merely
// hid the base virtual (signature drift, e.g. a missing const), the
// interface call would return the default empty array. Rows written via
// the concrete class must read back through IMemory*, newest first.
void TestMemory::usageEventsReachableThroughInterface() {
    SqliteMemory memory(":memory:");
    memory.addUsageEvent(QStringLiteral("nexus.user"),
                         QStringLiteral("session.create"),
                         QStringLiteral("s1"),
                         quint64{42},
                         QStringLiteral("count=1"));
    memory.addUsageEvent(QStringLiteral("nexus.network"),
                         QStringLiteral("llm_result"),
                         QStringLiteral("https://api.example.com/v1"),
                         quint64{43},
                         QString());

    IMemory* iface = &memory;
    const QJsonArray events = iface->loadUsageEvents(500);
    QCOMPARE(events.size(), 2);

    // Newest first (id DESC), full field contract on both rows.
    const QJsonObject newest = events.at(0).toObject();
    QCOMPARE(newest.value("action").toString(), QStringLiteral("llm_result"));
    QCOMPARE(newest.value("category").toString(), QStringLiteral("nexus.network"));
    QCOMPARE(newest.value("target").toString(),
             QStringLiteral("https://api.example.com/v1"));
    QCOMPARE(newest.value("requestId").toDouble(), 43.0);
    QVERIFY(newest.value("timestamp").toVariant().toLongLong() > 0);
    QVERIFY(newest.value("detail").toString().isEmpty());

    const QJsonObject oldest = events.at(1).toObject();
    QCOMPARE(oldest.value("action").toString(), QStringLiteral("session.create"));
    QCOMPARE(oldest.value("category").toString(), QStringLiteral("nexus.user"));
    QCOMPARE(oldest.value("target").toString(), QStringLiteral("s1"));
    QCOMPARE(oldest.value("requestId").toDouble(), 42.0);
    QCOMPARE(oldest.value("detail").toString(), QStringLiteral("count=1"));

    // Interface-path limit semantics: caps rows, keeps the newest.
    const QJsonArray capped = iface->loadUsageEvents(1);
    QCOMPARE(capped.size(), 1);
    QCOMPARE(capped.at(0).toObject().value("action").toString(),
             QStringLiteral("llm_result"));
}

// Batch 17: the trigram FTS path must answer 3+ char CJK/ASCII substring
// queries across sessions. Batch 22 (search-aggregate): one row per
// session, newest hit first, carrying the aggregated fields the sidebar
// renders (hitCount / latestTimestamp / bestSnippet / bestRole).
void TestMemory::searchMessagesFindsAcrossSessions() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("s1", "明代研究");
    mem.saveMessage("s1", msg("user", "帮我查一下明代的文献学资料"));
    mem.beginSession("s2", "杂谈");
    mem.saveMessage("s2", msg("assistant", "今天聊点别的，不谈学术"));
    mem.saveMessage("s2", msg("user", "其实我对文献学也有兴趣"));

    const QVariantList hits = mem.searchMessages(QStringLiteral("文献学"), 50);
    QCOMPARE(hits.size(), 2);
    // Newest hit first: s2's hit (saved last) precedes s1's.
    const QVariantMap first = hits.at(0).toMap();
    QCOMPARE(first.value("sessionId").toString(), QStringLiteral("s2"));
    QCOMPARE(first.value("sessionTitle").toString(), QStringLiteral("杂谈"));
    QCOMPARE(first.value("hitCount").toInt(), 1);
    QCOMPARE(first.value("bestRole").toString(), QStringLiteral("user"));
    QVERIFY(first.value("latestTimestamp").toLongLong() > 0);
    QVERIFY(first.value("bestSnippet").toString().contains(QStringLiteral("\u0001文献学\u0002")));
    // Grouping caption: the session title rides along.
    const QVariantMap second = hits.at(1).toMap();
    QCOMPARE(second.value("sessionId").toString(), QStringLiteral("s1"));
    QCOMPARE(second.value("sessionTitle").toString(), QStringLiteral("明代研究"));

    // Empty/blank keywords → empty results (sidebar treats it as "no search").
    QVERIFY(mem.searchMessages(QString(), 50).isEmpty());
    QVERIFY(mem.searchMessages(QStringLiteral("   "), 50).isEmpty());
}

// Deleting a session must remove its rows from the FTS index (DELETE trigger).
// Batch 22 (search-aggregate): one aggregated session row before the
// delete, none after.
void TestMemory::searchMessagesSyncsOnDelete() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("s1", "To be deleted");
    mem.saveMessage("s1", msg("user", "独一无二的检索锚点文本"));
    const QVariantList hits = mem.searchMessages(QStringLiteral("检索锚点"), 50);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.at(0).toMap().value("sessionId").toString(), QStringLiteral("s1"));
    QCOMPARE(hits.at(0).toMap().value("hitCount").toInt(), 1);

    mem.deleteSession("s1");
    QVERIFY(mem.searchMessages(QStringLiteral("检索锚点"), 50).isEmpty());
}

// docs/21：按"第 n 条回答"分叉。message_id 故意留空 / 填脏值，钉住分叉不读那一列。
void TestMemory::forkSessionAtAnswerCopiesThroughThatAnswer() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("src", QStringLiteral("源会话"));
    const qint64 q1 = mem.saveMessage("src", msg("user", QStringLiteral("第一问")));
    const qint64 a1 = mem.saveMessage("src", msg("assistant", QStringLiteral("第一答")));
    const qint64 q2 = mem.saveMessage("src", msg("user", QStringLiteral("第二问")));
    const qint64 a2 = mem.saveMessage("src", msg("assistant", QStringLiteral("第二答")));
    QVERIFY(q1 > 0 && a1 > q1 && q2 > a1 && a2 > q2);

    mem.saveTraceNode("src", QJsonObject{{QStringLiteral("id"), QStringLiteral("nq1")},
                                        {QStringLiteral("type"), QStringLiteral("user_message")},
                                        {QStringLiteral("title"), QStringLiteral("第一问")},
                                        {QStringLiteral("status"), QStringLiteral("completed")},
                                        {QStringLiteral("timestamp"), 1}}, 0);
    mem.saveTraceNode("src", QJsonObject{{QStringLiteral("id"), QStringLiteral("na1")},
                                        {QStringLiteral("type"), QStringLiteral("summary")},
                                        {QStringLiteral("title"), QStringLiteral("第一答")},
                                        {QStringLiteral("status"), QStringLiteral("completed")},
                                        {QStringLiteral("timestamp"), 1}}, 1);
    mem.saveTraceNode("src", QJsonObject{{QStringLiteral("id"), QStringLiteral("nm1")},
                                        {QStringLiteral("type"), QStringLiteral("metrics")},
                                        {QStringLiteral("title"), QStringLiteral("Task 1s")},
                                        {QStringLiteral("status"), QStringLiteral("completed")},
                                        {QStringLiteral("timestamp"), 1}}, 2);
    mem.saveTraceNode("src", QJsonObject{{QStringLiteral("id"), QStringLiteral("nq2")},
                                        {QStringLiteral("type"), QStringLiteral("user_message")},
                                        {QStringLiteral("title"), QStringLiteral("第二问")},
                                        {QStringLiteral("status"), QStringLiteral("completed")},
                                        {QStringLiteral("messageId"), a1}}, 3);
    mem.saveTraceNode("src", QJsonObject{{QStringLiteral("id"), QStringLiteral("na2")},
                                        {QStringLiteral("type"), QStringLiteral("summary")},
                                        {QStringLiteral("title"), QStringLiteral("第二答")},
                                        {QStringLiteral("status"), QStringLiteral("completed")},
                                        {QStringLiteral("timestamp"), 1},
                                        {QStringLiteral("messageId"), q2}}, 4);

    const QString forked = mem.forkSessionAtAnswer("src", 1, QStringLiteral("分支标题"));
    QVERIFY(!forked.isEmpty());
    const QJsonArray copied = mem.loadMessages(forked, 50);
    QCOMPARE(copied.size(), 2);
    QCOMPARE(copied.at(0).toObject().value("content").toString(), QStringLiteral("第一问"));
    QCOMPARE(copied.at(1).toObject().value("content").toString(), QStringLiteral("第一答"));
    QVERIFY(copied.at(0).toObject().value("id").toVariant().toLongLong() != q1);
    const QJsonArray nodes = mem.loadTraceNodes(forked);
    QCOMPARE(nodes.size(), 3);
    QCOMPARE(nodes.at(0).toObject().value("id").toString(), QStringLiteral("nq1"));
    QCOMPARE(nodes.at(2).toObject().value("type").toString(), QStringLiteral("metrics"));
    QVERIFY(!nodes.at(1).toObject().contains(QStringLiteral("messageId")));

    bool found = false;
    for (const auto& entry : mem.listSessions()) {
        const QJsonObject session = entry.toObject();
        if (session.value(QStringLiteral("id")).toString() == forked) {
            found = true;
            QCOMPARE(session.value(QStringLiteral("title")).toString(),
                     QStringLiteral("分支标题"));
        }
    }
    QVERIFY(found);

    QCOMPARE(mem.loadMessages("src", 50).size(), 4);
    QCOMPARE(mem.loadTraceNodes("src").size(), 5);

    // 从最后一答分叉：它后面没有提问了，切到时间线末尾（整段历史都在副本里）。
    const QString whole = mem.forkSessionAtAnswer("src", 2, QStringLiteral("全量分支"));
    QVERIFY(!whole.isEmpty());
    QCOMPARE(mem.loadMessages(whole, 50).size(), 4);
    QCOMPARE(mem.loadTraceNodes(whole).size(), 5);

    QVERIFY(mem.forkSessionAtAnswer("src", 3, QStringLiteral("x")).isEmpty());
    QVERIFY(mem.forkSessionAtAnswer("src", 0, QStringLiteral("x")).isEmpty());
    QVERIFY(mem.forkSessionAtAnswer("src", -1, QStringLiteral("x")).isEmpty());
    QVERIFY(mem.forkSessionAtAnswer(QString(), 1, QStringLiteral("x")).isEmpty());
}

// 分叉出的会话要能再次分叉且过程不丢：上一版按 message_id 切，而副本归属被清零，
// 第二次分叉一段过程都复制不出来。
void TestMemory::forkedSessionForksAgainKeepingProcess() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("src", QStringLiteral("源会话"));
    mem.saveMessage("src", msg("user", QStringLiteral("第一问")));
    mem.saveMessage("src", msg("assistant", QStringLiteral("第一答")));
    mem.saveMessage("src", msg("user", QStringLiteral("第二问")));
    mem.saveMessage("src", msg("assistant", QStringLiteral("第二答")));
    mem.saveTraceNode("src", QJsonObject{{QStringLiteral("id"), QStringLiteral("nq1")},
                                        {QStringLiteral("type"), QStringLiteral("user_message")},
                                        {QStringLiteral("title"), QStringLiteral("第一问")},
                                        {QStringLiteral("status"), QStringLiteral("completed")},
                                        {QStringLiteral("timestamp"), 1}}, 0);
    mem.saveTraceNode("src", QJsonObject{{QStringLiteral("id"), QStringLiteral("nt1")},
                                        {QStringLiteral("type"), QStringLiteral("thinking")},
                                        {QStringLiteral("title"), QStringLiteral("想想")},
                                        {QStringLiteral("status"), QStringLiteral("completed")},
                                        {QStringLiteral("timestamp"), 1}}, 1);
    mem.saveTraceNode("src", QJsonObject{{QStringLiteral("id"), QStringLiteral("na1")},
                                        {QStringLiteral("type"), QStringLiteral("summary")},
                                        {QStringLiteral("title"), QStringLiteral("第一答")},
                                        {QStringLiteral("status"), QStringLiteral("completed")},
                                        {QStringLiteral("timestamp"), 1}}, 2);

    const QString first = mem.forkSessionAtAnswer("src", 1, QStringLiteral("一次分支"));
    QVERIFY(!first.isEmpty());
    const QString second = mem.forkSessionAtAnswer(first, 1, QStringLiteral("二次分支"));
    QVERIFY(!second.isEmpty());
    QCOMPARE(mem.loadMessages(second, 50).size(), 2);
    // 思考过程跟着一起复制，副本不会退化成纯文本时间线。
    const QJsonArray nodes = mem.loadTraceNodes(second);
    QCOMPARE(nodes.size(), 3);
    QCOMPARE(nodes.at(1).toObject().value("type").toString(), QStringLiteral("thinking"));
}

// docs/21：中间那版消息树要能拉平回线性历史，且不弄丢用户当时在看的那条线。
// 按 head 的祖先链定序：链外的行是已被放弃的重复答案版本（删），挂在被删行上的
// 轨迹节点一起删（否则重开会话会画出一段没有归属答案的过程）。
void TestMemory::treeDatabaseFlattensToLinearHistory() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                        + "/nexusai-flat-test-" + QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(dir);
    const QString dbPath = dir + "/tree.db";
    QFile::remove(dbPath);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "treeRaw");
        db.setDatabaseName(dbPath);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE sessions (id TEXT PRIMARY KEY, title TEXT NOT NULL, "
                       "created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL, "
                       "head_message_id INTEGER NOT NULL DEFAULT 0)"));
        QVERIFY(q.exec("CREATE TABLE messages (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "session_id TEXT NOT NULL, parent_id INTEGER, role TEXT NOT NULL, "
                       "content TEXT NOT NULL, timestamp INTEGER NOT NULL, metadata TEXT)"));
        QVERIFY(q.exec("CREATE TABLE trace_nodes (session_id TEXT NOT NULL, node_id TEXT NOT NULL, "
                       "node_order INTEGER NOT NULL, type TEXT NOT NULL, title TEXT NOT NULL, "
                       "status TEXT NOT NULL, args TEXT NOT NULL, result TEXT NOT NULL, "
                       "timestamp INTEGER NOT NULL, message_id INTEGER NOT NULL DEFAULT 0, "
                       "metadata TEXT, PRIMARY KEY (session_id, node_id))"));
        QVERIFY(q.exec("CREATE TABLE message_variants (session_id TEXT NOT NULL, "
                       "group_key TEXT NOT NULL, variant_index INTEGER NOT NULL, "
                       "content TEXT NOT NULL, metadata TEXT, created_at INTEGER NOT NULL, "
                       "PRIMARY KEY (session_id, group_key, variant_index))"));
        QVERIFY(q.exec("INSERT INTO sessions VALUES ('s1','树',1,1,5)"));
        struct Row { int id; const char* role; const char* content; int parent; };
        // 3 是被放弃的另一个答案版本：不在 5→4→2→1 这条链上
        const QList<Row> rows = {
            {1, "user", "问题一", 0}, {2, "assistant", "当前答案一", 1},
            {3, "assistant", "被放弃的答案一", 1},
            {4, "user", "问题二", 2}, {5, "assistant", "答案二", 4},
        };
        for (const auto& row : rows) {
            q.prepare("INSERT INTO messages (id, session_id, parent_id, role, content, timestamp, metadata) "
                      "VALUES (?, 's1', ?, ?, ?, ?, '{}')");
            q.addBindValue(row.id);
            q.addBindValue(row.parent > 0 ? QVariant(row.parent) : QVariant());
            q.addBindValue(QString::fromUtf8(row.role));
            q.addBindValue(QString::fromUtf8(row.content));
            q.addBindValue(1000 + row.id);
            QVERIFY(q.exec());
        }
        struct Node { const char* id; int order; int owner; };
        const QList<Node> nodes = {{"n1",0,1},{"n2",1,2},{"n3",2,3},{"n4",3,4},{"n5",4,5}};
        for (const auto& node : nodes) {
            q.prepare("INSERT INTO trace_nodes (session_id, node_id, node_order, type, title, "
                      "status, args, result, timestamp, message_id, metadata) "
                      "VALUES ('s1', ?, ?, 'summary', 't', 'completed', '', '', 1, ?, '{}')");
            q.addBindValue(QLatin1String(node.id));
            q.addBindValue(node.order);
            q.addBindValue(node.owner);
            QVERIFY(q.exec());
        }
        QVERIFY(q.exec("INSERT INTO message_variants VALUES ('s1','1',0,'被放弃的答案一','{}',900)"));
        QVERIFY(q.exec("PRAGMA user_version = 2"));
        db.close();
    }
    QSqlDatabase::removeDatabase("treeRaw");

    {
        SqliteMemory mem(dbPath);
        const QJsonArray loaded = mem.loadMessages("s1", 50);
        QCOMPARE(loaded.size(), 4);                 // 链外那条重复答案被删
        QStringList contents;
        for (const auto& entry : loaded) contents << entry.toObject().value("content").toString();
        QVERIFY(!contents.contains(QStringLiteral("被放弃的答案一")));
        QVERIFY(contents.contains(QStringLiteral("当前答案一")));

        // 时间线里挂在被删行上的节点一起走（n3 归 row 3）。
        QStringList keptNodes;
        for (const auto& entry : mem.loadTraceNodes("s1"))
            keptNodes << entry.toObject().value("id").toString();
        QCOMPARE(keptNodes, (QStringList() << QStringLiteral("n1") << QStringLiteral("n2")
                                          << QStringLiteral("n4") << QStringLiteral("n5")));

        // 树列不再被读写：清空后照常线性工作。
        QSqlQuery probe(mem.databaseForTest());
        QVERIFY(probe.exec("SELECT COUNT(1) FROM messages WHERE parent_id IS NOT NULL"));
        QVERIFY(probe.next());
        QCOMPARE(probe.value(0).toInt(), 0);
        QVERIFY(!probe.exec("SELECT COUNT(1) FROM message_variants"));   // 表已回收

        const qint64 added = mem.saveMessage("s1", msg("user", QStringLiteral("问题三")));
        QVERIFY(added > 0);
        QCOMPARE(mem.loadMessages("s1", 50).size(), 5);
    }

    // 迁移幂等：再开一次不会重复删任何东西。
    SqliteMemory reopened(dbPath);
    QCOMPARE(reopened.loadMessages("s1", 50).size(), 5);
    QFile::remove(dbPath);
    QDir(dir).removeRecursively();
}

// 主键必须由写入方回传：靠 MAX(id) 反查的话，插入失败时会拿到上一条消息的 id，
// 于是分叉锚点指错轮次、截断阈值切错位置。
void TestMemory::saveMessageReturnsPrimaryKeyOrZero() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("s1", QStringLiteral("主键"));

    const qint64 first = mem.saveMessage("s1", msg("user", QStringLiteral("一")));
    const qint64 second = mem.saveMessage("s1", msg("assistant", QStringLiteral("二")));
    QVERIFY(first > 0);
    QCOMPARE(second > first, true);

    // 空会话 id 写进去就是一行永远取不到的孤儿。
    QCOMPARE(mem.saveMessage(QString(), msg("user", QStringLiteral("无会话"))), qint64(0));
    // role/content 是 NOT NULL 列：缺键的整节点写入必须失败返回 0。
    QCOMPARE(mem.saveMessage("s1", QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}}),
             qint64(0));
    QCOMPARE(mem.loadMessages("s1").size(), 2);
}

// 截断末轮必须把这一轮之后的持久化轨迹节点一起删掉。上一版只删消息行，被丢弃的
// 思考/工具过程会在重开会话时整段复活（界面上凭空多出没有归属答案的一串卡片）。
void TestMemory::truncateLastTurnPrunesPersistedTraceNodes() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("s1", QStringLiteral("截断"));
    const auto saveNode = [&mem](const QString& id, const QString& type, int order) {
        mem.saveTraceNode(QStringLiteral("s1"),
                          QJsonObject{{QStringLiteral("id"), id},
                                      {QStringLiteral("type"), type},
                                      {QStringLiteral("title"), id},
                                      {QStringLiteral("status"), QStringLiteral("completed")},
                                      {QStringLiteral("timestamp"), 1}},
                          order);
    };
    const qint64 q1 = mem.saveMessage("s1", msg("user", QStringLiteral("问题一")));
    const qint64 a1 = mem.saveMessage("s1", msg("assistant", QStringLiteral("答案一")));
    QVERIFY(q1 > 0 && a1 > q1);
    saveNode("nq1", "user_message", 0);
    saveNode("na1", "summary", 1);
    const qint64 q2 = mem.saveMessage("s1", msg("user", QStringLiteral("问题二")));
    const qint64 a2 = mem.saveMessage("s1", msg("assistant", QStringLiteral("答案二")));
    QVERIFY(q2 > 0 && a2 > q2);
    saveNode("nq2", "user_message", 2);
    saveNode("nthink", "thinking", 3);
    saveNode("nsum2", "summary", 4);
    QCOMPARE(mem.loadTraceNodes("s1").size(), 5);

    // 重新生成：末轮（问题二的答案、思考、过程）整体收口，提问留着当重发锚点；
    // 问题一那轮完整留下。
    QVERIFY(mem.truncateLastTurn("s1", /*removeUser=*/false));
    QStringList kept;
    for (const auto& entry : mem.loadTraceNodes("s1"))
        kept << entry.toObject().value("id").toString();
    QCOMPARE(kept, (QStringList() << QStringLiteral("nq1") << QStringLiteral("na1")
                                  << QStringLiteral("nq2")));
    QJsonArray afterRegen = mem.loadMessages("s1", 50);
    QCOMPARE(afterRegen.size(), 3);            // q1 / a1 / q2（q2 是重发锚点）

    // 编辑重发：连提问一起截掉。
    QVERIFY(mem.truncateLastTurn("s1", true));
    QCOMPARE(mem.loadMessages("s1", 50).size(), 2);
    QStringList afterEdit;
    for (const auto& entry : mem.loadTraceNodes("s1"))
        afterEdit << entry.toObject().value("id").toString();
    QCOMPARE(afterEdit, (QStringList() << QStringLiteral("nq1") << QStringLiteral("na1")));
    QVERIFY(!mem.truncateLastTurn(QString(), false));
}


// docs/14 L3: an in-place upgrade must convert an old episodic_fts (default
// unicode61 tokenizer) to trigram. Fresh databases get trigram straight from
// createTables, so this only exercises the migration path - exactly the case
// that silently never applied in production (drop failed with
// "database table is locked", so the recreate hit "already exists").
void TestMemory::episodicFtsUpgradesToTrigramOnOpen() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("episodic_upgrade.db"));
    {
        SqliteMemory memory(path);
        QSqlDatabase db = memory.databaseForTest();
        // Roll episodic_fts back to the pre-L3 shape: no trigram tokenizer.
        QVERIFY(db.exec("DROP TRIGGER IF EXISTS episodic_ai").isActive());
        QVERIFY(db.exec("DROP TRIGGER IF EXISTS episodic_ad").isActive());
        QVERIFY(db.exec("DROP TABLE IF EXISTS episodic_fts").isActive());
        QVERIFY(db.exec("CREATE VIRTUAL TABLE episodic_fts USING fts5("
                        "summary, tags, content='episodic', content_rowid='id')")
                    .isActive());
        QSqlQuery pre(db);
        QVERIFY(pre.exec("SELECT sql FROM sqlite_master WHERE name='episodic_fts'"));
        QVERIFY(pre.next());
        QVERIFY(!pre.value(0).toString().contains(QStringLiteral("trigram")));
    }
    {
        // Reopening runs migrateEpisodicFts(): the schema must become trigram.
        SqliteMemory memory(path);
        QSqlDatabase db = memory.databaseForTest();
        QSqlQuery q(db);
        QVERIFY(q.exec("SELECT sql FROM sqlite_master WHERE name='episodic_fts'"));
        QVERIFY(q.next());
        QVERIFY2(q.value(0).toString().contains(QStringLiteral("trigram")),
                 qPrintable(QStringLiteral("episodic_fts not upgraded: %1")
                                .arg(q.value(0).toString())));
        // The sync triggers must survive the drop/recreate round trip.
        QSqlQuery trig(db);
        QVERIFY(trig.exec("SELECT count(*) FROM sqlite_master WHERE type='trigger' "
                          "AND name IN ('episodic_ai','episodic_ad')"));
        QVERIFY(trig.next());
        QCOMPARE(trig.value(0).toInt(), 2);
    }
}

// Keywords shorter than 3 characters cannot use the trigram index; they must
// fall back to a LIKE scan (both CJK and ASCII). Both rows share one
// session, so the aggregated result is a single session row either way.
void TestMemory::searchMessagesShortKeywordLikeFallback() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.saveMessage("s1", msg("user", "明代史料汇编"));
    mem.saveMessage("s1", msg("assistant", "rust async runtime notes"));

    const QVariantList cjk = mem.searchMessages(QStringLiteral("明代"), 50);
    QCOMPARE(cjk.size(), 1);
    QVERIFY(cjk.at(0).toMap().value("bestSnippet").toString()
                .contains(QStringLiteral("\u0001明代\u0002")));

    // 5 chars → the trigram FTS path; same one-row-per-session shape and
    // the same [kw] marker pipeline.
    const QVariantList ascii = mem.searchMessages(QStringLiteral("async"), 50);
    QCOMPARE(ascii.size(), 1);
    QVERIFY(ascii.at(0).toMap().value("bestSnippet").toString()
                .contains(QStringLiteral("\u0001async\u0002")));

    QVERIFY(mem.searchMessages(QStringLiteral("zzz"), 50).isEmpty());
}

// Batch 21 (A3) kept same-session rows contiguous; Batch 22
// (search-aggregate) collapses them entirely: ONE row per session, ordered
// by the group's newest hit, with the best snippet (user role first, then
// newest) as the session's single preview line.
void TestMemory::searchMessagesGroupsBySession() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("sA", "Session A");
    mem.beginSession("sB", "Session B");

    // Insert directly so timestamps are deterministic (saveMessage stamps now).
    // Time order of matching messages: B1(400), A1(300), B2(200), A2(100).
    // Session B's newest hit (400) outranks A's (300), so B leads.
    QSqlQuery insert(mem.databaseForTest());
    QVERIFY(insert.prepare(
        "INSERT INTO messages (session_id, role, content, timestamp) "
        "VALUES (?, ?, ?, ?)"));
    auto addRow = [&](const QString& session, const QString& content, qint64 ts) {
        insert.bindValue(0, session);
        insert.bindValue(1, QStringLiteral("user"));
        insert.bindValue(2, content);
        insert.bindValue(3, ts);
        QVERIFY2(insert.exec(), qPrintable(insert.lastError().text()));
    };
    addRow("sA", QStringLiteral("分组关键词-A-old"), 100);
    addRow("sB", QStringLiteral("分组关键词-B-old"), 200);
    addRow("sA", QStringLiteral("分组关键词-A-new"), 300);
    addRow("sB", QStringLiteral("分组关键词-B-new"), 400);

    const QVariantList hits = mem.searchMessages(QStringLiteral("分组关键词"), 50);
    QCOMPARE(hits.size(), 2);
    const QVariantMap sB = hits.at(0).toMap();
    QCOMPARE(sB.value("sessionId").toString(), QStringLiteral("sB"));
    QCOMPARE(sB.value("hitCount").toInt(), 2);
    QCOMPARE(sB.value("latestTimestamp").toLongLong(), qint64(400));
    QVERIFY(sB.value("bestSnippet").toString().contains(QStringLiteral("B-new")));
    QVERIFY(sB.value("bestSnippet").toString().contains(QStringLiteral("\u0001分组关键词\u0002")));
    const QVariantMap sA = hits.at(1).toMap();
    QCOMPARE(sA.value("sessionId").toString(), QStringLiteral("sA"));
    QCOMPARE(sA.value("hitCount").toInt(), 2);
    QCOMPARE(sA.value("latestTimestamp").toLongLong(), qint64(300));
    QVERIFY(sA.value("bestSnippet").toString().contains(QStringLiteral("A-new")));
}

// Batch 21 (B4): past the LIKE row ceiling, a sub-trigram keyword must NOT run
// the full scan; instead exactly one notice row is returned.
void TestMemory::searchMessagesLikeGuardSuppressesLargeTables() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("s1", "Large library");

    // Bulk-insert just over the 50000-row ceiling via a single transaction.
    {
        QSqlQuery q(mem.databaseForTest());
        QVERIFY(q.exec("BEGIN"));
        QVERIFY(q.prepare(
            "INSERT INTO messages (session_id, role, content, timestamp) "
            "VALUES ('s1', 'user', ?, ?)"));
        for (int i = 0; i < 50005; ++i) {
            q.bindValue(0, QStringLiteral("padding-row-%1").arg(i));
            q.bindValue(1, static_cast<qint64>(i));
            QVERIFY2(q.exec(), qPrintable(q.lastError().text()));
        }
        QVERIFY(q.exec("COMMIT"));
    }

    const QVariantList guarded = mem.searchMessages(QStringLiteral("ab"), 50);
    QCOMPARE(guarded.size(), 1);
    const QVariantMap notice = guarded.at(0).toMap();
    QCOMPARE(notice.value("sessionId").toString(),
             QStringLiteral("__search_notice__"));
    QVERIFY(notice.value("notice").toString().contains(QStringLiteral("全文索引")));

    // The FTS path (3+ chars) is unaffected by the guard. Use saveMessage so
    // the content goes through the normal path.
    mem.saveMessage("s1", msg("user", "可索引的独特关键词锚点"));
    QCOMPARE(mem.searchMessages(QStringLiteral("独特关键词"), 50).size(), 1);
}

// ── Batch 22 (search-aggregate) ────────────────────────────────────────
// Aggregation correctness: multiple sessions with multiple hits each
// collapse to one row per session; hitCount is the exact per-session match
// count and latestTimestamp is that session's newest matching message.
void TestMemory::searchMessagesAggregatePerSession() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("s1", "第一会话");
    mem.beginSession("s2", "第二会话");
    mem.beginSession("s3", "第三会话");

    // Deterministic timestamps (saveMessage stamps "now", which can tie).
    QSqlQuery insert(mem.databaseForTest());
    QVERIFY(insert.prepare(
        "INSERT INTO messages (session_id, role, content, timestamp) "
        "VALUES (?, ?, ?, ?)"));
    auto addRow = [&](const QString& session, const QString& role,
                      const QString& content, qint64 ts) {
        insert.bindValue(0, session);
        insert.bindValue(1, role);
        insert.bindValue(2, content);
        insert.bindValue(3, ts);
        QVERIFY2(insert.exec(), qPrintable(insert.lastError().text()));
    };
    // s1: three hits (100/200/600), s2: two hits (300/500), s3: one (400).
    addRow("s1", "user", "聚合关键词 one", 100);
    addRow("s1", "assistant", "聚合关键词 two", 200);
    addRow("s2", "user", "聚合关键词 three", 300);
    addRow("s3", "user", "聚合关键词 four", 400);
    addRow("s2", "assistant", "聚合关键词 five", 500);
    addRow("s1", "assistant", "聚合关键词 six", 600);

    const QVariantList hits = mem.searchMessages(QStringLiteral("聚合关键词"), 50);
    QCOMPARE(hits.size(), 3);
    // Ordered by each session's newest hit: s1(600) > s2(500) > s3(400).
    QCOMPARE(hits.at(0).toMap().value("sessionId").toString(), QStringLiteral("s1"));
    QCOMPARE(hits.at(0).toMap().value("hitCount").toInt(), 3);
    QCOMPARE(hits.at(0).toMap().value("latestTimestamp").toLongLong(), qint64(600));
    QCOMPARE(hits.at(0).toMap().value("sessionTitle").toString(), QStringLiteral("第一会话"));
    QCOMPARE(hits.at(1).toMap().value("sessionId").toString(), QStringLiteral("s2"));
    QCOMPARE(hits.at(1).toMap().value("hitCount").toInt(), 2);
    QCOMPARE(hits.at(1).toMap().value("latestTimestamp").toLongLong(), qint64(500));
    QCOMPARE(hits.at(2).toMap().value("sessionId").toString(), QStringLiteral("s3"));
    QCOMPARE(hits.at(2).toMap().value("hitCount").toInt(), 1);
    QCOMPARE(hits.at(2).toMap().value("latestTimestamp").toLongLong(), qint64(400));

    // Non-matching rows in the same sessions must not inflate hitCount.
    addRow("s1", "user", "完全无关的内容", 700);
    const QVariantList after = mem.searchMessages(QStringLiteral("聚合关键词"), 50);
    QCOMPARE(after.size(), 3);
    QCOMPARE(after.at(0).toMap().value("hitCount").toInt(), 3);

    // A1 (hit-locate): firstHitMessageId must point to each session's newest hit.
    // s1 newest = id 6 (ts=600), s2 newest = id 5 (ts=500), s3 newest = id 4 (ts=400).
    QCOMPARE(after.at(0).toMap().value("firstHitMessageId").toLongLong(), qint64(6));
    QCOMPARE(after.at(1).toMap().value("firstHitMessageId").toLongLong(), qint64(5));
    QCOMPARE(after.at(2).toMap().value("firstHitMessageId").toLongLong(), qint64(4));
}

// Ordering and limit semantics: newest-hit session first; `limit` caps the
// number of SESSIONS returned (it used to cap message rows).
void TestMemory::searchMessagesOrdersSessionsAndCapsLimit() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("old", "旧会话");
    mem.beginSession("mid", "中会话");
    mem.beginSession("new", "新会话");

    QSqlQuery insert(mem.databaseForTest());
    QVERIFY(insert.prepare(
        "INSERT INTO messages (session_id, role, content, timestamp) "
        "VALUES (?, ?, ?, ?)"));
    auto addRow = [&](const QString& session, qint64 ts) {
        insert.bindValue(0, session);
        insert.bindValue(1, QStringLiteral("user"));
        insert.bindValue(2, QStringLiteral("排序关键词-%1").arg(session));
        insert.bindValue(3, ts);
        QVERIFY2(insert.exec(), qPrintable(insert.lastError().text()));
    };
    addRow("old", 100);
    addRow("mid", 200);
    addRow("new", 300);

    const QVariantList ordered = mem.searchMessages(QStringLiteral("排序关键词"), 50);
    QCOMPARE(ordered.size(), 3);
    QCOMPARE(ordered.at(0).toMap().value("sessionId").toString(), QStringLiteral("new"));
    QCOMPARE(ordered.at(1).toMap().value("sessionId").toString(), QStringLiteral("mid"));
    QCOMPARE(ordered.at(2).toMap().value("sessionId").toString(), QStringLiteral("old"));

    // limit = 2 keeps only the two newest-hit sessions.
    const QVariantList capped = mem.searchMessages(QStringLiteral("排序关键词"), 2);
    QCOMPARE(capped.size(), 2);
    QCOMPARE(capped.at(0).toMap().value("sessionId").toString(), QStringLiteral("new"));
    QCOMPARE(capped.at(1).toMap().value("sessionId").toString(), QStringLiteral("mid"));
}

// Best-snippet preference: within a session a user-role hit wins over a
// newer assistant hit (people usually search their own words), the newest
// user hit at that; a session with assistant hits only falls back to its
// newest one.
void TestMemory::searchMessagesBestSnippetPrefersUserHit() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("s1", "用户优先");
    mem.beginSession("s2", "仅助手");

    QSqlQuery insert(mem.databaseForTest());
    QVERIFY(insert.prepare(
        "INSERT INTO messages (session_id, role, content, timestamp) "
        "VALUES (?, ?, ?, ?)"));
    auto addRow = [&](const QString& session, const QString& role,
                      const QString& content, qint64 ts) {
        insert.bindValue(0, session);
        insert.bindValue(1, role);
        insert.bindValue(2, content);
        insert.bindValue(3, ts);
        QVERIFY2(insert.exec(), qPrintable(insert.lastError().text()));
    };
    addRow("s1", "assistant", "偏好关键词 assistant-old", 100);
    addRow("s1", "user", "偏好关键词 user-old", 200);
    addRow("s1", "assistant", "偏好关键词 assistant-new", 300);
    addRow("s1", "user", "偏好关键词 user-new", 400);
    addRow("s2", "assistant", "偏好关键词 only-assistant", 500);

    const QVariantList hits = mem.searchMessages(QStringLiteral("偏好关键词"), 50);
    QCOMPARE(hits.size(), 2);
    // s2's newest hit (500) outranks s1's (400).
    const QVariantMap s2 = hits.at(0).toMap();
    QCOMPARE(s2.value("sessionId").toString(), QStringLiteral("s2"));
    QCOMPARE(s2.value("bestRole").toString(), QStringLiteral("assistant"));
    QVERIFY(s2.value("bestSnippet").toString().contains(QStringLiteral("only-assistant")));
    // s1: the newest USER hit beats the newest assistant hit.
    const QVariantMap s1 = hits.at(1).toMap();
    QCOMPARE(s1.value("sessionId").toString(), QStringLiteral("s1"));
    QCOMPARE(s1.value("hitCount").toInt(), 4);
    QCOMPARE(s1.value("bestRole").toString(), QStringLiteral("user"));
    QVERIFY(s1.value("bestSnippet").toString().contains(QStringLiteral("user-new")));
}

// Markdown stripping: the snippet shows plain text - decorations
// (**bold**, `code`, headings, list bullets, links) are stripped BEFORE
// the [keyword] marker is injected (the link rule would otherwise eat
// "[kw]" as link text), and the keyword is wrapped in half-width [ ].
void TestMemory::searchMessagesSnippetStripsMarkdown() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("s1", "Markdown");
    mem.saveMessage("s1", msg("user",
        "关于**量子纠缠**的解释：\n"
        "- 见 `量子线路` 文档\n"
        "## 量子纠缠小节\n"
        "详情参见 [量子纠缠百科](https://example.com/qe)"));

    const QVariantList hits = mem.searchMessages(QStringLiteral("量子纠缠"), 50);
    QCOMPARE(hits.size(), 1);
    const QString snippet = hits.at(0).toMap().value("bestSnippet").toString();
    QVERIFY(!snippet.contains(QStringLiteral("**")));
    QVERIFY(!snippet.contains(QLatin1Char('`')));
    QVERIFY(!snippet.contains(QStringLiteral("##")));
    QVERIFY(!snippet.contains(QStringLiteral("- ")));
    QVERIFY(!snippet.contains(QStringLiteral("https://")));
    QVERIFY(snippet.contains(QStringLiteral("\u0001量子纠缠\u0002")));
}

// Newline flattening: a multi-line message (\n, \r\n, \r mix) yields a
// single-line snippet - embedded line breaks used to overflow the
// sidebar's fixed row heights.
void TestMemory::searchMessagesSnippetFlattensNewlines() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("s1", "多行");
    mem.saveMessage("s1", msg("assistant",
        "量子筛法第一行\n量子筛法第二行\r\n量子筛法第三行\r量子筛法第四行"));

    const QVariantList hits = mem.searchMessages(QStringLiteral("量子筛法"), 50);
    QCOMPARE(hits.size(), 1);
    const QString snippet = hits.at(0).toMap().value("bestSnippet").toString();
    QVERIFY(!snippet.contains(QLatin1Char('\n')));
    QVERIFY(!snippet.contains(QLatin1Char('\r')));
    QVERIFY(snippet.contains(QStringLiteral("\u0001量子筛法\u0002")));
}

// Path consistency: the trigram FTS path (3+ chars) and the LIKE fallback
// (<3 chars) go through the same snippet pipeline, so both wrap the
// keyword in the same [ ] markers.
void TestMemory::searchMessagesFtsAndLikeAgreeOnMarkers() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("s1", "一致性");
    mem.saveMessage("s1", msg("user", "超导量子比特的相干时间"));

    // 4 chars → trigram FTS path.
    const QVariantList fts = mem.searchMessages(QStringLiteral("量子比特"), 50);
    QCOMPARE(fts.size(), 1);
    QCOMPARE(fts.at(0).toMap().value("hitCount").toInt(), 1);
    QVERIFY(fts.at(0).toMap().value("bestSnippet").toString()
                .contains(QStringLiteral("\u0001量子比特\u0002")));

    // 2 chars → LIKE fallback path.
    const QVariantList like = mem.searchMessages(QStringLiteral("超导"), 50);
    QCOMPARE(like.size(), 1);
    QCOMPARE(like.at(0).toMap().value("hitCount").toInt(), 1);
    QVERIFY(like.at(0).toMap().value("bestSnippet").toString()
                .contains(QStringLiteral("\u0001超导\u0002")));
}

// Archived filtering: archived sessions drop out of the results unless
// includeArchived is set (same session-set discipline as listSessions).
void TestMemory::searchMessagesRespectsArchivedFilter() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("visible", "常规会话");
    mem.beginSession("boxed", "归档会话");
    mem.saveMessage("visible", msg("user", "归档过滤关键词 A"));
    mem.saveMessage("boxed", msg("user", "归档过滤关键词 B"));
    QVERIFY(mem.setSessionArchived("boxed", true));

    const QVariantList active =
        mem.searchMessages(QStringLiteral("归档过滤关键词"), 50);
    QCOMPARE(active.size(), 1);
    QCOMPARE(active.at(0).toMap().value("sessionId").toString(),
             QStringLiteral("visible"));

    const QVariantList all =
        mem.searchMessages(QStringLiteral("归档过滤关键词"), 50, true);
    QCOMPARE(all.size(), 2);

    // A1 (hit-locate): firstHitMessageId must be present and valid for each session.
    QVERIFY(all.at(0).toMap().value("firstHitMessageId").toLongLong() > 0);
    QVERIFY(all.at(1).toMap().value("firstHitMessageId").toLongLong() > 0);
}

// Review fix (M-1) regression: underscore-flavored markers must not be
// stripped from code identifiers - "__init__" and "user_name" appear far
// more often as code in chat messages than as bold/italic markup.
void TestMemory::searchMessagesSnippetPreservesUnderscoreIdentifiers() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("s1", "代码会话");
    mem.saveMessage("s1", msg("assistant",
        "在 Python 中 __init__ 是构造函数，user_name 是下划线命名"));
    const QVariantList hits = mem.searchMessages(QStringLiteral("user_name"), 50);
    QCOMPARE(hits.size(), 1);
    const QString snippet = hits.at(0).toMap().value("bestSnippet").toString();
    QVERIFY(snippet.contains(QStringLiteral("user_name")));      // 标识符完整保留
    QVERIFY(snippet.contains(QStringLiteral("\u0001user_name\u0002")));  // 高亮标记在位
    QVERIFY(snippet.contains(QStringLiteral("__init__")));       // dunder 完整保留
}

// Review fix (degradation path): a keyword that only exists inside
// stripped markup (here: the URL of an image) must produce a marker-less
// head-window snippet instead of crashing or losing the row.
void TestMemory::searchMessagesSnippetDegradesWhenKeywordInStrippedMarkup() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("s1", "图片会话");
    mem.saveMessage("s1", msg("assistant",
        "架构图说明：![系统架构](https://img.example.com/diagram.png) 如图所示"));
    // "img.example" 只存在于被剥离的图片 URL 中。
    const QVariantList hits = mem.searchMessages(QStringLiteral("img.example"), 50);
    QCOMPARE(hits.size(), 1);
    const QString snippet = hits.at(0).toMap().value("bestSnippet").toString();
    QVERIFY(!snippet.contains(QChar(u'\u0001')));   // 无标记
    QVERIFY(!snippet.contains(QStringLiteral("https://")));  // URL 已被剥离
}

// Discipline #8: an old database without messages_fts must self-heal on
// open - the FTS index is created and pre-existing rows are backfilled.
void TestMemory::messagesFtsMigrationSelfHeals() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                        + "/nexusai-memtest-" + QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(dir);
    const QString dbPath = dir + "/pre17.db";
    QFile::remove(dbPath);

    {
        // v2-era database: messages exist, messages_fts does not
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "pre17-raw");
        db.setDatabaseName(dbPath);
        QVERIFY(db.open());
        QSqlQuery q(db);
        QVERIFY(q.exec("CREATE TABLE messages (id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "session_id TEXT NOT NULL, role TEXT NOT NULL, content TEXT NOT NULL, "
                       "timestamp INTEGER NOT NULL, metadata TEXT)"));
        q.prepare("INSERT INTO messages (session_id, role, content, timestamp) "
                  "VALUES (?, ?, ?, ?)");
        q.addBindValue("old-session");
        q.addBindValue("user");
        q.addBindValue("旧库里沉睡的检索锚点");
        q.addBindValue(1234567);
        QVERIFY(q.exec());
        db.close();
    }
    QSqlDatabase::removeDatabase("pre17-raw");

    SqliteMemory memory(dbPath);
    const QVariantList hits = memory.searchMessages(QStringLiteral("检索锚点"), 50);
    QCOMPARE(hits.size(), 1);
    QCOMPARE(hits.at(0).toMap().value("sessionId").toString(), QStringLiteral("old-session"));

    // New writes keep flowing through the trigger after the migration.
    memory.saveMessage("new-session", msg("user", "迁移之后新写入的检索锚点"));
    QCOMPARE(memory.searchMessages(QStringLiteral("检索锚点"), 50).size(), 2);

    QFile::remove(dbPath);
    QDir(dir).removeRecursively();
}

// docs/23 G1: the two highest-frequency query paths must stay index-backed —
// pins the query plan against a future schema change silently dropping them.
void TestMemory::sessionQueriesUseIndexes() {
    SqliteMemory memory(":memory:");
    QSqlDatabase db = memory.databaseForTest();

    const auto planUses = [&db](const QString& sql, const QString& index) -> bool {
        QSqlQuery plan(db);
        if (!plan.exec(QStringLiteral("EXPLAIN QUERY PLAN %1").arg(sql)))
            return false;
        while (plan.next()) {
            if (plan.value(3).toString().contains(index)) return true;
        }
        return false;
    };

    QVERIFY(planUses(QStringLiteral(
        "SELECT role FROM messages WHERE session_id = 's' "
        "ORDER BY timestamp DESC, rowid DESC"),
        QStringLiteral("idx_messages_session_ts")));
    QVERIFY(planUses(QStringLiteral(
        "SELECT id FROM sessions ORDER BY pinned DESC, updated_at DESC"),
        QStringLiteral("idx_sessions_pinned_updated")));
}

// docs/23 G1: a pre-stamp database (user_version=0) upgrades in place —
// indexes are recreated, the one-shot FTS backfill runs, user_version
// lands on 1 (subsequent opens skip the O(messages) backfill).
void TestMemory::oldDatabaseUpgradeRestampsAndBackfills() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("upgrade.db"));
    {
        SqliteMemory memory(path);
        QSqlDatabase db = memory.databaseForTest();
        QSqlQuery insert(db);
        QVERIFY(insert.exec(
            "INSERT INTO messages(session_id, role, content, timestamp) "
            "VALUES('s1', 'user', 'backfill me', 123)"));
        // Simulate the pre-stamp state: desync FTS, drop the new index.
        QVERIFY(db.exec("DELETE FROM messages_fts").isActive());
        QVERIFY(db.exec("DROP INDEX IF EXISTS idx_messages_session_ts").isActive());
        QVERIFY(db.exec("PRAGMA user_version = 0").isActive());
    }
    {
        SqliteMemory memory(path);
        QSqlDatabase db = memory.databaseForTest();

        QSqlQuery version(db);
        QVERIFY(version.exec("PRAGMA user_version") && version.next());
        // docs/21 真分支：消息树迁移把闸门推到 2（老库从 0/1 升上来都一样）。
        QCOMPARE(version.value(0).toInt(), 3);

        QSqlQuery index(db);
        QVERIFY(index.exec("SELECT name FROM sqlite_master "
                           "WHERE type='index' AND name='idx_messages_session_ts'"));
        QVERIFY(index.next());

        QSqlQuery fts(db);
        QVERIFY(fts.exec("SELECT count(*) FROM messages_fts"));
        QVERIFY(fts.next());
        QCOMPARE(fts.value(0).toInt(), 1);   // backfill restored the row
    }
}

// docs/25 B1: idx_episodic_ts / idx_episodic_session back the semantic
// scan's candidate-capped ORDER BY timestamp. They are created on fresh
// databases and re-added in place (CREATE INDEX IF NOT EXISTS) when a
// pre-B1 database without them is reopened — no dump/reload needed.
void TestMemory::episodicIndexesPresentAndUpgraded() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("episodic_idx.db"));
    {
        SqliteMemory memory(path);
        QSqlQuery q(memory.databaseForTest());
        QVERIFY(q.exec("SELECT count(*) FROM sqlite_master WHERE type='index' "
                       "AND name IN ('idx_episodic_ts','idx_episodic_session')"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 2);
        // Simulate a pre-B1 database: indexes missing, rows intact.
        QVERIFY(q.exec("DROP INDEX idx_episodic_ts"));
        QVERIFY(q.exec("DROP INDEX idx_episodic_session"));
    }
    {
        // Reopen: createTables() re-adds both indexes without touching data.
        SqliteMemory memory(path);
        QSqlQuery q(memory.databaseForTest());
        QVERIFY(q.exec("SELECT count(*) FROM sqlite_master WHERE type='index' "
                       "AND name IN ('idx_episodic_ts','idx_episodic_session')"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toInt(), 2);
        QVERIFY(q.exec("PRAGMA integrity_check"));
        QVERIFY(q.next());
        QCOMPARE(q.value(0).toString(), QStringLiteral("ok"));
    }
}

// docs/25 B4: clear(sessionId) and purgeDeletedSessions() now run inside a
// single transaction. The observable contract is unchanged: clear wipes the
// session's messages AND episodic rows in one shot; purge hard-deletes only
// the expired soft-deleted sessions — live sessions stay fully intact.
void TestMemory::clearAndPurgeStayAtomic() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("s1", "live session");
    mem.beginSession("s2", "doomed session");
    mem.saveMessage("s1", msg("user", "stay"));
    mem.saveMessage("s2", msg("user", "gone"));

    QJsonObject episode;
    episode["summary"] = QStringLiteral("live session episode");
    mem.saveEpisodic("s1", episode);
    episode["summary"] = QStringLiteral("doomed session episode");
    mem.saveEpisodic("s2", episode);

    mem.clear("s2");
    QCOMPARE(mem.loadMessages("s2", 50).size(), 0);
    QCOMPARE(mem.searchEpisodic(QStringLiteral("doomed"), 10).size(), 0);
    QCOMPARE(mem.loadMessages("s1", 50).size(), 1);
    QCOMPARE(mem.searchEpisodic(QStringLiteral("live session episode"), 10).size(), 1);

    // Soft-delete s2, then purge with a deadline that already covers it.
    mem.deleteSession("s2");
    QCOMPARE(mem.purgeDeletedSessions(QDateTime::currentMSecsSinceEpoch() + 1000), 1);
    QCOMPARE(mem.listSessions(10).size(), 1);
    QCOMPARE(mem.loadMessages("s1", 50).size(), 1);
    QVERIFY(mem.loadMessages("s2", 50).isEmpty());
}

// docs/23 G2: the WAL journal mode is the correctness basis for the
// multi-tab connection topology (readers never block the writer) and for
// VACUUM INTO backups (raw .db copies are invalid under WAL). WAL needs a
// file-backed database — :memory: silently keeps the "memory" journal.
void TestMemory::walJournalModeActive() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    SqliteMemory memory(dir.filePath(QStringLiteral("wal.db")));
    QSqlQuery pragma(memory.databaseForTest());
    QVERIFY(pragma.exec("PRAGMA journal_mode"));
    QVERIFY(pragma.next());
    QCOMPARE(pragma.value(0).toString(), QStringLiteral("wal"));
}

// docs/23 G3: audit_log / token_usage prune by age — boundary rows straddle
// the cutoff; keepDays <= 0 is "keep forever" and must be a no-op.
void TestMemory::retentionPrunesAuditAndTokenUsage() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    QSqlDatabase db = mem.databaseForTest();
    const qint64 now = QDateTime::currentDateTime().toMSecsSinceEpoch();
    const qint64 day = 86400 * 1000;

    const auto insertAudit = [&](qint64 ts) {
        QSqlQuery q(db);
        QVERIFY(q.prepare("INSERT INTO audit_log(session_id, event, tool_name, "
                          "arguments, success, detail, timestamp) "
                          "VALUES('s', 'tool_execute', 'read_file', '', 1, '', ?)"));
        q.addBindValue(ts);
        QVERIFY(q.exec());
    };
    const auto insertToken = [&](qint64 ts) {
        QSqlQuery q(db);
        QVERIFY(q.prepare("INSERT INTO token_usage(request_id, session_id, model, "
                          "timestamp) VALUES(1, 's', 'm', ?)"));
        q.addBindValue(ts);
        QVERIFY(q.exec());
    };
    insertAudit(now - 40 * day);   // expired by a 30-day policy
    insertAudit(now - 29 * day);   // inside the window
    insertAudit(now);
    insertToken(now - 100 * day);  // expired by a 90-day policy
    insertToken(now - 89 * day);
    insertToken(now);

    QCOMPARE(mem.pruneAudit(30), 1);
    QCOMPARE(mem.pruneTokenUsage(90), 1);

    QSqlQuery audit(db);
    QVERIFY(audit.exec("SELECT count(*) FROM audit_log") && audit.next());
    QCOMPARE(audit.value(0).toInt(), 2);
    QSqlQuery tokens(db);
    QVERIFY(tokens.exec("SELECT count(*) FROM token_usage") && tokens.next());
    QCOMPARE(tokens.value(0).toInt(), 2);

    // keepDays <= 0 = keep forever: nothing removed, nothing touched.
    QCOMPARE(mem.pruneAudit(0), 0);
    QCOMPARE(mem.pruneTokenUsage(-1), 0);
}

// docs/23 G6: clear() must cascade to trace nodes like deleteSession —
// a surviving trajectory would resurrect ghost nodes on session reopen.
void TestMemory::clearRemovesTraceNodes() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("s1", "First");
    QJsonObject node;
    node["id"] = QStringLiteral("n1");
    node["type"] = QStringLiteral("thinking");
    node["title"] = QStringLiteral("Reasoning");
    node["status"] = QStringLiteral("completed");
    node["args"] = QString();
    node["timestamp"] = 1000.0;
    mem.saveTraceNode("s1", node, 0);
    QVERIFY(!mem.loadTraceNodes(QStringLiteral("s1")).isEmpty());

    mem.clear("s1");
    QVERIFY(mem.loadTraceNodes(QStringLiteral("s1")).isEmpty());
}

// docs/23 G7: soft delete hides the session from the list and from message
// search (both FTS and LIKE paths join sessions), the undo window keeps the
// rows, restoreSession unmarks, and purgeDeletedSessions hard-cascades the
// four tables while leaving the audit chain intact.
void TestMemory::deletedSessionRoundTrip() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("s1", "Doomed");
    mem.saveMessage("s1", msg("user", QStringLiteral("删除撤销检索锚点")));
    QJsonObject node;
    node["id"] = QStringLiteral("n1");
    node["type"] = QStringLiteral("thinking");
    node["status"] = QStringLiteral("completed");
    node["args"] = QString();
    node["timestamp"] = 1000.0;
    mem.saveTraceNode("s1", node, 0);
    mem.logAudit("s1", "task_complete", "", "", true, "done", 1, 10, 5, "m");

    mem.deleteSession("s1");
    QCOMPARE(mem.listSessions(10).size(), 0);
    QVERIFY(mem.searchMessages(QStringLiteral("检索锚点"), 10).isEmpty());
    QVERIFY(mem.loadMessages("s1", 10).size() > 0);   // undo window

    QVERIFY(mem.restoreSession("s1"));
    QCOMPARE(mem.listSessions(10).size(), 1);
    QCOMPARE(mem.searchMessages(QStringLiteral("检索锚点"), 10).size(), 1);

    // Expired: hard cascade removes messages/trace/episodic + the session
    // row; the audit chain stays (regenerate/undo history is kept by design).
    mem.deleteSession("s1");
    QCOMPARE(mem.purgeDeletedSessions(
                 QDateTime::currentMSecsSinceEpoch() + 1000), 1);
    QCOMPARE(mem.listSessions(10).size(), 0);
    QVERIFY(mem.loadMessages("s1", 10).isEmpty());
    QVERIFY(mem.loadTraceNodes(QStringLiteral("s1")).isEmpty());
    QSqlQuery q(mem.databaseForTest());
    QVERIFY(q.exec("SELECT count(*) FROM sessions") && q.next());
    QCOMPARE(q.value(0).toInt(), 0);
    QVERIFY(q.exec("SELECT count(*) FROM audit_log") && q.next());
    QCOMPARE(q.value(0).toInt(), 1);
}

// docs/23 D1: the VACUUM INTO snapshot opens as a fully working database
// with identical row counts, passes quick_check, and the auto_* rotation
// keeps only the newest N files (manual_* untouched).
void TestMemory::backupRoundTripInspectAndRotate() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString dbPath = dir.filePath(QStringLiteral("source.db"));
    {
        SqliteMemory memory(dbPath);
        memory.beginSession("s1", "Backed up");
        memory.saveMessage("s1", msg("user", "snapshot me"));
    }

    const QString backupsDir = dir.filePath(QStringLiteral("backups"));
    QVERIFY(QDir().mkpath(backupsDir));
    const QString target = backupsDir + QStringLiteral("/auto_20260101_000000.db");
    QVERIFY(SqliteMemory::createBackupFile(dbPath, target));

    // The snapshot is a live database: counts match, integrity ok.
    const QVariantMap info = SqliteMemory::inspectBackupFile(target);
    QCOMPARE(info.value("error").toString(), QString());
    QCOMPARE(info.value("sessions").toLongLong(), 1LL);
    QCOMPARE(info.value("messages").toLongLong(), 1LL);
    QVERIFY(info.value("integrityOk").toBool());

    {
        SqliteMemory fromBackup(target);
        QCOMPARE(fromBackup.listSessions(10).size(), 1);
        QCOMPARE(fromBackup.loadMessages("s1", 10).size(), 1);
    }

    // Rotation: five auto snapshots + one manual → newest 3 autos survive.
    for (int i = 2; i <= 6; ++i) {
        QFile f(backupsDir + QStringLiteral("/auto_20260101_00000%1.db").arg(i));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }
    QFile manual(backupsDir + QStringLiteral("/manual_20260101_000000.db"));
    QVERIFY(manual.open(QIODevice::WriteOnly));
    manual.write("keep me");
    manual.close();

    QCOMPARE(SqliteMemory::rotateAutoBackups(backupsDir, 3), 3);
    QVERIFY(QFile::exists(backupsDir + QStringLiteral("/auto_20260101_000004.db")));
    QVERIFY(QFile::exists(backupsDir + QStringLiteral("/auto_20260101_000005.db")));
    QVERIFY(QFile::exists(backupsDir + QStringLiteral("/auto_20260101_000006.db")));
    QVERIFY(!QFile::exists(backupsDir + QStringLiteral("/auto_20260101_000003.db")));
    QVERIFY(manual.exists());   // manual snapshots are never rotated
}

// docs/24 R1: the restore primitive swaps the live file for a verified
// snapshot — same-instance data switches, the instance stays writable, and
// the rollback anchor is cleaned up on success.
void TestMemory::restoreFromBackupRoundTrip() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString snapshot = dir.filePath(QStringLiteral("snap.db"));
    const QString livePath = dir.filePath(QStringLiteral("live.db"));
    // Snapshot content: two sessions.
    {
        SqliteMemory writer(snapshot);
        writer.beginSession("a1", "One");
        writer.saveMessage("a1", msg("user", "from snapshot one"));
        writer.beginSession("a2", "Two");
        writer.saveMessage("a2", msg("user", "from snapshot two"));
    }

    // Live database with DIFFERENT content.
    SqliteMemory live(livePath);
    live.beginSession("b1", "Live");
    live.saveMessage("b1", msg("user", "live data"));
    QCOMPARE(live.listSessions(10).size(), 1);

    QVERIFY(live.restoreFromBackup(snapshot));

    QCOMPARE(live.listSessions(10).size(), 2);
    QCOMPARE(live.loadMessages("a1", 10).size(), 1);
    QVERIFY(!QFile::exists(livePath + QStringLiteral(".pre-restore")));
    QVERIFY(!QFile::exists(livePath + QStringLiteral(".restore-tmp")));
    // The reopened instance is fully usable for new writes.
    live.beginSession("c1", "After restore");
    QCOMPARE(live.listSessions(10).size(), 3);
}

// docs/24 R1: a corrupt candidate snapshot is refused before anything
// moves — original data untouched, connection still usable.
void TestMemory::restoreFromBackupRejectsCorruptSnapshot() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString garbage = dir.filePath(QStringLiteral("garbage.db"));
    {
        QFile f(garbage);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("this is not a sqlite database at all");
    }

    SqliteMemory live(dir.filePath(QStringLiteral("live.db")));
    live.beginSession("b1", "Live");
    live.saveMessage("b1", msg("user", "live data"));

    QVERIFY(!live.restoreFromBackup(garbage));
    QCOMPARE(live.listSessions(10).size(), 1);
    QCOMPARE(live.loadMessages("b1", 10).size(), 1);
    live.beginSession("b2", "Still writable");
    QCOMPARE(live.listSessions(10).size(), 2);
}

// docs/23 G5: dropping the messages table makes loadMessages' exec fail —
// the function must still return (empty array) and, since this fix, log
// the error instead of failing silently (the warning lands in the log).
void TestMemory::loadMessagesFailureIsLoggedNotSilent() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    QSqlDatabase db = mem.databaseForTest();
    QVERIFY(db.exec("DROP TABLE messages").isActive());

    QCOMPARE(mem.loadMessages(QStringLiteral("s1")).size(), 0);
}

// G4: checkpoint metadata/content round-trip, session scoping, and undo.
void TestMemory::fileCheckpointLifecycle() {
    SqliteMemory mem(QStringLiteral(":memory:"));

    const qint64 id1 = mem.addFileCheckpoint(
        QStringLiteral("s1"), QStringLiteral("/p/a.txt"),
        QByteArray("old"), true, false, 42);
    QVERIFY(id1 > 0);
    const qint64 id2 = mem.addFileCheckpoint(
        QStringLiteral("s1"), QStringLiteral("/p/b.txt"),
        QByteArray(), false, false, 43);
    QVERIFY(id2 > id1);

    // Newest first, metadata only.
    const QJsonArray rows = mem.listFileCheckpoints(QStringLiteral("s1"));
    QCOMPARE(rows.size(), 2);
    const QJsonObject newest = rows.at(0).toObject();
    QCOMPARE(newest.value("path").toString(), QStringLiteral("/p/b.txt"));
    QVERIFY(!newest.value("existed").toBool());
    QVERIFY(!newest.value("truncated").toBool());
    QVERIFY(!newest.value("undone").toBool());
    QCOMPARE(newest.value("requestId").toInt(), 43);
    QVERIFY(newest.value("byteSize").toInt() == 0);

    QByteArray before;
    QString path;
    bool existed = false;
    bool truncated = true;
    QVERIFY(mem.loadFileCheckpointContent(id1, QStringLiteral("s1"), &before,
                                          &existed, &truncated, &path));
    QCOMPARE(path, QStringLiteral("/p/a.txt"));
    QCOMPARE(before, QByteArray("old"));
    QVERIFY(existed);
    QVERIFY(!truncated);

    // Session scoping: another session cannot read the row.
    QVERIFY(!mem.loadFileCheckpointContent(id1, QStringLiteral("other"), &before,
                                           &existed, &truncated, &path));

    QVERIFY(mem.markFileCheckpointUndone(id1, QStringLiteral("s1"),
                                         QDateTime::currentMSecsSinceEpoch()));
    QVERIFY(!mem.markFileCheckpointUndone(id1, QStringLiteral("s1"),
                                          QDateTime::currentMSecsSinceEpoch()));
    // Undo drops the content (the row stays as history).
    QVERIFY(!mem.loadFileCheckpointContent(id1, QStringLiteral("s1"), &before,
                                           &existed, &truncated, &path));
    const QJsonArray after = mem.listFileCheckpoints(QStringLiteral("s1"));
    QVERIFY(after.at(1).toObject().value("undone").toBool());
    QCOMPARE(after.at(1).toObject().value("byteSize").toInt(), 0);
}

// G4: 30-day age prune and the hard session purge both remove checkpoints;
// a soft delete keeps them so session undo still has its history.
void TestMemory::fileCheckpointPrunedByAgeAndSessionPurge() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    QVERIFY(mem.addFileCheckpoint(QStringLiteral("s1"), QStringLiteral("/p/a"),
                                  QByteArray("a"), true, false) > 0);
    QCOMPARE(mem.pruneFileCheckpoints(now - 24 * 60 * 60 * 1000), 0);
    QCOMPARE(mem.pruneFileCheckpoints(now + 1000), 1);
    QCOMPARE(mem.listFileCheckpoints(QStringLiteral("s1")).size(), 0);

    mem.beginSession(QStringLiteral("s2"), QStringLiteral("t"));
    QVERIFY(mem.addFileCheckpoint(QStringLiteral("s2"), QStringLiteral("/p/b"),
                                  QByteArray("b"), true, false) > 0);
    mem.deleteSession(QStringLiteral("s2"));
    // Soft delete (undo window) keeps the checkpoint.
    QCOMPARE(mem.listFileCheckpoints(QStringLiteral("s2")).size(), 1);
    QCOMPARE(mem.purgeDeletedSessions(now + 1000), 1);
    QCOMPARE(mem.listFileCheckpoints(QStringLiteral("s2")).size(), 0);
}

// G4: once the per-session content budget is exhausted, further captures are
// recorded as path-only rows (truncated=1) instead of growing the DB without
// bound. The single-file cap is covered by test_tools.
void TestMemory::fileCheckpointSessionBudgetDegradesToPathOnly() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    const QByteArray megabyte(1024 * 1024, 'x');

    // 32 MiB fits exactly.
    for (int i = 0; i < 32; ++i) {
        QVERIFY2(mem.addFileCheckpoint(
                     QStringLiteral("s-budget"),
                     QStringLiteral("/p/%1.bin").arg(i), megabyte,
                     true, false) > 0,
                 qPrintable(QStringLiteral("row %1").arg(i)));
    }
    const qint64 overflow = mem.addFileCheckpoint(
        QStringLiteral("s-budget"), QStringLiteral("/p/overflow.bin"),
        megabyte, true, false);
    QVERIFY(overflow > 0);

    const QJsonArray rows = mem.listFileCheckpoints(QStringLiteral("s-budget"));
    QCOMPARE(rows.size(), 33);
    const QJsonObject newest = rows.at(0).toObject();
    QCOMPARE(newest.value("path").toString(), QStringLiteral("/p/overflow.bin"));
    QVERIFY(newest.value("truncated").toBool());
    QCOMPARE(newest.value("byteSize").toInt(), 0);

    QByteArray before;
    QString path;
    bool existed = false;
    bool truncated = false;
    QVERIFY(mem.loadFileCheckpointContent(overflow, QStringLiteral("s-budget"),
                                          &before, &existed, &truncated, &path));
    QVERIFY(truncated);
    QVERIFY(before.isEmpty());
}

QTEST_GUILESS_MAIN(TestMemory)
#include "test_memory.moc"