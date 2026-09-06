// SqliteMemory: message/session persistence and FTS5 episodic search.
#include "core/memory/SqliteMemory.h"
#include "core/rag/KnowledgeChunk.h"
#include <QDir>
#include <QStandardPaths>
#include <QSqlDatabase>
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
    void messageRoundTrip();
    void loadMessagesKeepsNewest();
    void sessionBookkeepingAndClear();
    void sessionDeleteAndRename();
    void episodicFullTextSearch();
    void auditTrail();
    void traceNodeRoundTrip();
    // Batch 6: knowledge persistence v2
    void knowledgeSchemaV1Migrates();
    void knowledgeChunkPersistence();
    void knowledgeCascadeDelete();
    // Batch 7: Conversation/Memory consistency
    void sqliteConnectionNameCleanedUp();
    void workspaceIdColumnReserved();
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

QTEST_GUILESS_MAIN(TestMemory)
#include "test_memory.moc"