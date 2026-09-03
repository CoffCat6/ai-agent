// SqliteMemory: message/session persistence and FTS5 episodic search.
#include "core/memory/SqliteMemory.h"

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

QTEST_GUILESS_MAIN(TestMemory)
#include "test_memory.moc"