// SqliteMemory: message/session persistence and FTS5 episodic search.
#include "core/memory/SqliteMemory.h"
#include "core/rag/KnowledgeChunk.h"
#include <QDir>
#include <QStandardPaths>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
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
    void episodicFullTextSearch();
    void auditTrail();
    void tokenStatsAggregate();
    void tokenStatsEmptyWhenNoData();
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
    // Batch 17: cross-session message search (FEAT-1701/1702)
    void searchMessagesFindsAcrossSessions();
    void searchMessagesSyncsOnDelete();
    void searchMessagesShortKeywordLikeFallback();
    void messagesFtsMigrationSelfHeals();
    // Batch 21: session-grouped ordering (A3) and LIKE guard (B4)
    void searchMessagesGroupsBySession();
    void searchMessagesLikeGuardSuppressesLargeTables();
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
    QCOMPARE(mem.loadMessages("s1", 10).size(), 0); // messages removed too
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

// Batch 17: the trigram FTS path must answer 3+ char CJK/ASCII substring
// queries across sessions, newest first, with session title enrichment.
void TestMemory::searchMessagesFindsAcrossSessions() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("s1", "明代研究");
    mem.saveMessage("s1", msg("user", "帮我查一下明代的文献学资料"));
    mem.beginSession("s2", "杂谈");
    mem.saveMessage("s2", msg("assistant", "今天聊点别的，不谈学术"));
    mem.saveMessage("s2", msg("user", "其实我对文献学也有兴趣"));

    const QVariantList hits = mem.searchMessages(QStringLiteral("文献学"), 50);
    QCOMPARE(hits.size(), 2);
    // Newest first: s2's hit precedes s1's.
    const QVariantMap first = hits.at(0).toMap();
    QCOMPARE(first.value("sessionId").toString(), QStringLiteral("s2"));
    QCOMPARE(first.value("role").toString(), QStringLiteral("user"));
    QVERIFY(first.value("messageId").toLongLong() > 0);
    QVERIFY(first.value("timestamp").toLongLong() > 0);
    QVERIFY(first.value("snippet").toString().contains(QStringLiteral("文献学")));
    // Grouping caption: the session title rides along.
    const QVariantMap second = hits.at(1).toMap();
    QCOMPARE(second.value("sessionId").toString(), QStringLiteral("s1"));
    QCOMPARE(second.value("sessionTitle").toString(), QStringLiteral("明代研究"));

    // Empty/blank keywords → empty results (sidebar treats it as "no search").
    QVERIFY(mem.searchMessages(QString(), 50).isEmpty());
    QVERIFY(mem.searchMessages(QStringLiteral("   "), 50).isEmpty());
}

// Deleting a session must remove its rows from the FTS index (DELETE trigger).
void TestMemory::searchMessagesSyncsOnDelete() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("s1", "To be deleted");
    mem.saveMessage("s1", msg("user", "独一无二的检索锚点文本"));
    QCOMPARE(mem.searchMessages(QStringLiteral("检索锚点"), 50).size(), 1);

    mem.deleteSession("s1");
    QVERIFY(mem.searchMessages(QStringLiteral("检索锚点"), 50).isEmpty());
}

// Keywords shorter than 3 characters cannot use the trigram index; they must
// fall back to a LIKE scan (both CJK and ASCII).
void TestMemory::searchMessagesShortKeywordLikeFallback() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.saveMessage("s1", msg("user", "明代史料汇编"));
    mem.saveMessage("s1", msg("assistant", "rust async runtime notes"));

    const QVariantList cjk = mem.searchMessages(QStringLiteral("明代"), 50);
    QCOMPARE(cjk.size(), 1);
    QVERIFY(cjk.at(0).toMap().value("snippet").toString()
                .contains(QStringLiteral("明代")));

    const QVariantList ascii = mem.searchMessages(QStringLiteral("async"), 50);
    QCOMPARE(ascii.size(), 1);

    QVERIFY(mem.searchMessages(QStringLiteral("zzz"), 50).isEmpty());
}

// Batch 21 (A3): hits interleaved by timestamp across sessions must still be
// returned in contiguous session groups (Sidebar section headers require it),
// ordered by the group's newest hit; within a group, newest first.
void TestMemory::searchMessagesGroupsBySession() {
    SqliteMemory mem(QStringLiteral(":memory:"));
    mem.beginSession("sA", "Session A");
    mem.beginSession("sB", "Session B");

    // Insert directly so timestamps are deterministic (saveMessage stamps now).
    // Time order of matching messages: B1(400), A1(300), B2(200), A2(100).
    // Session A's newest hit (300) is newer than B's newest (400)? No - B's
    // group max is 400, so B leads; within B: 400 then 200; then A: 300, 100.
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
    QCOMPARE(hits.size(), 4);
    QCOMPARE(hits.at(0).toMap().value("sessionId").toString(), QStringLiteral("sB"));
    QVERIFY(hits.at(0).toMap().value("snippet").toString().contains(QStringLiteral("B-new")));
    QCOMPARE(hits.at(1).toMap().value("sessionId").toString(), QStringLiteral("sB"));
    QVERIFY(hits.at(1).toMap().value("snippet").toString().contains(QStringLiteral("B-old")));
    QCOMPARE(hits.at(2).toMap().value("sessionId").toString(), QStringLiteral("sA"));
    QVERIFY(hits.at(2).toMap().value("snippet").toString().contains(QStringLiteral("A-new")));
    QCOMPARE(hits.at(3).toMap().value("sessionId").toString(), QStringLiteral("sA"));
    QVERIFY(hits.at(3).toMap().value("snippet").toString().contains(QStringLiteral("A-old")));
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

QTEST_GUILESS_MAIN(TestMemory)
#include "test_memory.moc"