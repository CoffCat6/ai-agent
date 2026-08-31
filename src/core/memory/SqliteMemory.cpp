#include "SqliteMemory.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QDateTime>
#include <QUuid>

namespace NexusAI {

namespace {

// Human-friendly relative timestamp for the session list
QString relativeTime(qint64 timestamp) {
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(timestamp);
    const qint64 secs = t.secsTo(now);
    if (secs < 60) return QStringLiteral("刚刚");
    if (secs < 3600) return QStringLiteral("%1 分钟前").arg(secs / 60);
    if (secs < 86400) return QStringLiteral("%1 小时前").arg(secs / 3600);
    if (secs < 172800) return QStringLiteral("昨天");
    return t.toString(QStringLiteral("MM-dd"));
}

} // namespace

SqliteMemory::SqliteMemory(const QString& dbPath) : m_dbPath(dbPath) {
    if (m_dbPath.isEmpty()) {
        QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dataDir);
        m_dbPath = dataDir + "/nexusai_memory.db";
    }

    m_db = QSqlDatabase::addDatabase("QSQLITE",
        QStringLiteral("nexus_memory_%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        qWarning() << "Failed to open memory database:" << m_db.lastError().text();
        return;
    }

    initDatabase();
}

SqliteMemory::~SqliteMemory() {
    if (m_db.isOpen()) {
        m_db.close();
    }
    m_db = QSqlDatabase(); // drop the handle before removing the connection
    if (!m_connName.isEmpty()) {
        QSqlDatabase::removeDatabase(m_connName);
    }
}

void SqliteMemory::initDatabase() {
    createTables();
}

void SqliteMemory::createTables() {
    QSqlQuery query(m_db);

    // Messages table
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            role TEXT NOT NULL,
            content TEXT NOT NULL,
            timestamp INTEGER NOT NULL,
            metadata TEXT
        )
    )");

    // Episodic memory table with FTS5
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS episodic (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            summary TEXT NOT NULL,
            tags TEXT,
            embedding BLOB,
            timestamp INTEGER NOT NULL
        )
    )");

    // FTS5 virtual table for full-text search
    query.exec(R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS episodic_fts USING fts5(
            summary, tags, content='episodic', content_rowid='id'
        )
    )");

    // Triggers to keep FTS in sync
    query.exec(R"(
        CREATE TRIGGER IF NOT EXISTS episodic_ai AFTER INSERT ON episodic BEGIN
            INSERT INTO episodic_fts(rowid, summary, tags)
            VALUES (new.id, new.summary, new.tags);
        END
    )");

    query.exec(R"(
        CREATE TRIGGER IF NOT EXISTS episodic_ad AFTER DELETE ON episodic BEGIN
            INSERT INTO episodic_fts(episodic_fts, rowid, summary, tags)
            VALUES('delete', old.id, old.summary, old.tags);
        END
    )");

    // Sessions table for the sidebar history
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS sessions (
            id TEXT PRIMARY KEY,
            title TEXT NOT NULL,
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL
        )
    )");

    // Audit trail for tool activity
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS audit_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            event TEXT NOT NULL,
            tool_name TEXT NOT NULL,
            arguments TEXT,
            success INTEGER NOT NULL,
            detail TEXT,
            timestamp INTEGER NOT NULL
        )
    )");
    query.exec("CREATE INDEX IF NOT EXISTS idx_audit_ts ON audit_log(timestamp)");

    // Knowledge-base documents (imported text, restored at startup)
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS knowledge_documents (
            name TEXT PRIMARY KEY,
            content TEXT NOT NULL,
            imported_at INTEGER NOT NULL
        )
    )");

    // Trace view nodes (thinking / tool calls / results) per session, so
    // switching conversations restores the full visual timeline
    query.exec(R"(
        CREATE TABLE IF NOT EXISTS trace_nodes (
            session_id TEXT NOT NULL,
            node_id TEXT NOT NULL,
            node_order INTEGER NOT NULL,
            type TEXT NOT NULL,
            title TEXT NOT NULL,
            status TEXT NOT NULL,
            args TEXT NOT NULL,
            result TEXT NOT NULL,
            timestamp INTEGER NOT NULL,
            PRIMARY KEY (session_id, node_id)
        )
    )");
    query.exec("CREATE INDEX IF NOT EXISTS idx_trace_nodes_session "
               "ON trace_nodes(session_id, node_order)");
}

// ── Knowledge-base persistence ───────────────────────────

void SqliteMemory::upsertKnowledgeDoc(const QString& name, const QString& content) {
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO knowledge_documents (name, content, imported_at) "
                  "VALUES (?, ?, ?) "
                  "ON CONFLICT(name) DO UPDATE SET content = excluded.content, "
                  "imported_at = excluded.imported_at");
    query.addBindValue(name);
    query.addBindValue(content);
    query.addBindValue(QDateTime::currentDateTime().toMSecsSinceEpoch());

    if (!query.exec()) {
        qWarning() << "Failed to save knowledge document:" << query.lastError().text();
    }
}

bool SqliteMemory::removeKnowledgeDoc(const QString& name) {
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM knowledge_documents WHERE name = ?");
    query.addBindValue(name);
    if (!query.exec()) {
        qWarning() << "Failed to remove knowledge document:" << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() > 0;
}

void SqliteMemory::clearKnowledgeDocs() {
    QSqlQuery query(m_db);
    if (!query.exec("DELETE FROM knowledge_documents")) {
        qWarning() << "Failed to clear knowledge documents:" << query.lastError().text();
    }
}

QString SqliteMemory::loadKnowledgeDoc(const QString& name) const {
    QSqlQuery query(m_db);
    query.prepare("SELECT content FROM knowledge_documents WHERE name = ?");
    query.addBindValue(name);

    if (query.exec() && query.next())
        return query.value(0).toString();
    return QString();
}

QJsonObject SqliteMemory::loadKnowledgeDocs() const {
    QSqlQuery query(m_db);
    query.prepare("SELECT name, content FROM knowledge_documents ORDER BY imported_at ASC");

    QJsonObject docs;
    if (query.exec()) {
        while (query.next()) {
            docs.insert(query.value(0).toString(), query.value(1).toString());
        }
    }
    return docs;
}

void SqliteMemory::saveMessage(const QString& sessionId, const QJsonObject& msg) {
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO messages (session_id, role, content, timestamp, metadata) "
                  "VALUES (?, ?, ?, ?, ?)");
    query.addBindValue(sessionId);
    query.addBindValue(msg.value("role").toString());
    query.addBindValue(msg.value("content").toString());
    query.addBindValue(QDateTime::currentDateTime().toMSecsSinceEpoch());
    query.addBindValue(QJsonDocument(msg.value("metadata").toObject()).toJson(QJsonDocument::Compact));

    if (!query.exec()) {
        qWarning() << "Failed to save message:" << query.lastError().text();
    }
}

QJsonArray SqliteMemory::loadMessages(const QString& sessionId, int limit) {
    QSqlQuery query(m_db);
    query.prepare("SELECT role, content, metadata FROM messages WHERE session_id = ? "
                  "ORDER BY timestamp ASC LIMIT ?");
    query.addBindValue(sessionId);
    query.addBindValue(limit);

    QJsonArray messages;
    if (query.exec()) {
        while (query.next()) {
            QJsonObject msg;
            msg["role"] = query.value(0).toString();
            msg["content"] = query.value(1).toString();
            msg["metadata"] = QJsonDocument::fromJson(query.value(2).toString().toUtf8()).object();
            messages.append(msg);
        }
    }
    return messages;
}

void SqliteMemory::saveEpisodic(const QString& sessionId, const QJsonObject& episode) {
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO episodic (session_id, summary, tags, embedding, timestamp) "
                  "VALUES (?, ?, ?, ?, ?)");
    query.addBindValue(sessionId);
    query.addBindValue(episode.value("summary").toString());
    query.addBindValue(episode.value("tags").toString());
    query.addBindValue(episode.value("embedding").toVariant().toByteArray());
    query.addBindValue(QDateTime::currentDateTime().toMSecsSinceEpoch());

    if (!query.exec()) {
        qWarning() << "Failed to save episodic memory:" << query.lastError().text();
    }
}

QJsonArray SqliteMemory::searchEpisodic(const QString& query, int limit) {
    QSqlQuery sqlQuery(m_db);
    sqlQuery.prepare("SELECT e.id, e.session_id, e.summary, e.tags, e.timestamp "
                     "FROM episodic_fts f JOIN episodic e ON f.rowid = e.id "
                     "WHERE episodic_fts MATCH ? ORDER BY rank LIMIT ?");
    sqlQuery.addBindValue(query);
    sqlQuery.addBindValue(limit);

    QJsonArray results;
    if (sqlQuery.exec()) {
        while (sqlQuery.next()) {
            QJsonObject episode;
            episode["id"] = sqlQuery.value(0).toInt();
            episode["session_id"] = sqlQuery.value(1).toString();
            episode["summary"] = sqlQuery.value(2).toString();
            episode["tags"] = sqlQuery.value(3).toString();
            episode["timestamp"] = sqlQuery.value(4).toLongLong();
            results.append(episode);
        }
    }
    return results;
}

void SqliteMemory::clear(const QString& sessionId) {
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM messages WHERE session_id = ?");
    query.addBindValue(sessionId);
    query.exec();

    query.prepare("DELETE FROM episodic WHERE session_id = ?");
    query.addBindValue(sessionId);
    query.exec();
}

void SqliteMemory::beginSession(const QString& sessionId, const QString& title) {
    QSqlQuery query(m_db);
    query.prepare("INSERT OR REPLACE INTO sessions (id, title, created_at, updated_at) "
                  "VALUES (?, ?, ?, ?)");
    const qint64 now = QDateTime::currentDateTime().toMSecsSinceEpoch();
    query.addBindValue(sessionId);
    query.addBindValue(title);
    query.addBindValue(now);
    query.addBindValue(now);

    if (!query.exec()) {
        qWarning() << "Failed to begin session:" << query.lastError().text();
    }
}

void SqliteMemory::touchSession(const QString& sessionId) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE sessions SET updated_at = ? WHERE id = ?");
    query.addBindValue(QDateTime::currentDateTime().toMSecsSinceEpoch());
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "Failed to touch session:" << query.lastError().text();
    }
}

QJsonArray SqliteMemory::listSessions(int limit) {
    QSqlQuery query(m_db);
    query.prepare("SELECT id, title, updated_at FROM sessions "
                  "ORDER BY updated_at DESC LIMIT ?");
    query.addBindValue(limit);

    QJsonArray sessions;
    if (query.exec()) {
        while (query.next()) {
            QJsonObject session;
            session["id"] = query.value(0).toString();
            session["title"] = query.value(1).toString();
            const qint64 updated = query.value(2).toLongLong();
            session["updatedAt"] = static_cast<double>(updated);
            session["timeText"] = relativeTime(updated);
            sessions.append(session);
        }
    } else {
        qWarning() << "Failed to list sessions:" << query.lastError().text();
    }
    return sessions;
}

void SqliteMemory::deleteSession(const QString& sessionId) {
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM messages WHERE session_id = ?");
    query.addBindValue(sessionId);
    query.exec();

    query.prepare("DELETE FROM episodic WHERE session_id = ?");
    query.addBindValue(sessionId);
    query.exec();

    // FTS shadow table keeps itself in sync via the delete trigger
    query.prepare("DELETE FROM sessions WHERE id = ?");
    query.addBindValue(sessionId);
    query.exec();

    query.prepare("DELETE FROM trace_nodes WHERE session_id = ?");
    query.addBindValue(sessionId);
    query.exec();
}

// ── Trace view persistence ───────────────────────────────

void SqliteMemory::saveTraceNode(const QString& sessionId, const QJsonObject& node, int order) {
    QSqlQuery query(m_db);
    // Upsert: position (node_order) is fixed by the first insert; later
    // saves of the same node id only refresh content/status.
    query.prepare("INSERT INTO trace_nodes (session_id, node_id, node_order, type, title, "
                  "status, args, result, timestamp) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) "
                  "ON CONFLICT(session_id, node_id) DO UPDATE SET type = excluded.type, "
                  "title = excluded.title, status = excluded.status, args = excluded.args, "
                  "result = excluded.result, timestamp = excluded.timestamp");
    query.addBindValue(sessionId);
    query.addBindValue(node.value("id").toString());
    query.addBindValue(order);
    query.addBindValue(node.value("type").toString());
    query.addBindValue(node.value("title").toString());
    query.addBindValue(node.value("status").toString());
    query.addBindValue(node.value("args").toString());
    query.addBindValue(node.value("result").toString());
    query.addBindValue(static_cast<qint64>(node.value("timestamp").toDouble()));

    if (!query.exec()) {
        qWarning() << "Failed to save trace node:" << query.lastError().text();
    }
}

QJsonArray SqliteMemory::loadTraceNodes(const QString& sessionId) {
    QSqlQuery query(m_db);
    query.prepare("SELECT node_id, type, title, status, args, result, timestamp, node_order "
                  "FROM trace_nodes WHERE session_id = ? ORDER BY node_order ASC");
    query.addBindValue(sessionId);

    QJsonArray nodes;
    if (query.exec()) {
        while (query.next()) {
            QJsonObject node;
            node["id"] = query.value(0).toString();
            node["type"] = query.value(1).toString();
            node["title"] = query.value(2).toString();
            node["status"] = query.value(3).toString();
            node["args"] = query.value(4).toString();
            node["result"] = query.value(5).toString();
            node["timestamp"] = query.value(6).toLongLong();
            node["order"] = query.value(7).toInt();
            nodes.append(node);
        }
    } else {
        qWarning() << "Failed to load trace nodes:" << query.lastError().text();
    }
    return nodes;
}

void SqliteMemory::renameSession(const QString& sessionId, const QString& title) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE sessions SET title = ?, updated_at = ? WHERE id = ?");
    query.addBindValue(title);
    query.addBindValue(QDateTime::currentDateTime().toMSecsSinceEpoch());
    query.addBindValue(sessionId);
    if (!query.exec()) {
        qWarning() << "Failed to rename session:" << query.lastError().text();
    }
}

void SqliteMemory::logAudit(const QString& sessionId, const QString& event,
                            const QString& toolName, const QString& arguments,
                            bool success, const QString& detail) {
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO audit_log (session_id, event, tool_name, arguments, "
                  "success, detail, timestamp) VALUES (?, ?, ?, ?, ?, ?, ?)");
    query.addBindValue(sessionId);
    query.addBindValue(event);
    query.addBindValue(toolName);
    query.addBindValue(arguments);
    query.addBindValue(success ? 1 : 0);
    query.addBindValue(detail);
    query.addBindValue(QDateTime::currentDateTime().toMSecsSinceEpoch());

    if (!query.exec()) {
        qWarning() << "Failed to write audit entry:" << query.lastError().text();
    }
}

QJsonArray SqliteMemory::auditLog(int limit) {
    QSqlQuery query(m_db);
    query.prepare("SELECT session_id, event, tool_name, arguments, success, detail, "
                  "timestamp FROM audit_log ORDER BY timestamp DESC, id DESC LIMIT ?");
    query.addBindValue(limit);

    QJsonArray entries;
    if (query.exec()) {
        while (query.next()) {
            QJsonObject entry;
            entry["sessionId"] = query.value(0).toString();
            entry["event"] = query.value(1).toString();
            entry["toolName"] = query.value(2).toString();
            entry["arguments"] = query.value(3).toString();
            entry["success"] = query.value(4).toInt() != 0;
            entry["detail"] = query.value(5).toString();
            entry["timestamp"] = query.value(6).toLongLong();
            entries.append(entry);
        }
    } else {
        qWarning() << "Failed to read audit log:" << query.lastError().text();
    }
    return entries;
}

} // namespace NexusAI
