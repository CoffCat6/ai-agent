#include "SqliteMemory.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QDateTime>

namespace NexusAI {

SqliteMemory::SqliteMemory(const QString& dbPath) : m_dbPath(dbPath) {
    if (m_dbPath.isEmpty()) {
        QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dataDir);
        m_dbPath = dataDir + "/nexusai_memory.db";
    }

    m_db = QSqlDatabase::addDatabase("QSQLITE", "nexus_memory");
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

int SqliteMemory::estimateTokens(const QString& text) {
    // Rough estimate: ~4 characters per token
    return text.length() / 4;
}

int SqliteMemory::estimateTokens(const QJsonArray& messages) {
    int total = 0;
    for (const auto& msg : messages) {
        QJsonObject obj = msg.toObject();
        total += estimateTokens(obj.value("content").toString());
        total += estimateTokens(obj.value("role").toString());
    }
    return total;
}

QJsonArray SqliteMemory::compressIfTooLong(const QJsonArray& messages, int maxTokens) {
    int currentTokens = estimateTokens(messages);
    if (currentTokens <= maxTokens) return messages;

    QJsonArray compressed;

    // 1. Keep system prompt (first message)
    if (!messages.isEmpty()) {
        compressed.append(messages.first());
    }

    // 2. Summarize middle messages (in a real implementation, call LLM)
    if (messages.size() > 11) {
        QJsonObject summaryMsg;
        summaryMsg["role"] = "system";
        summaryMsg["content"] = "[Previous conversation has been compressed. "
                                "Key points: ...]";
        compressed.append(summaryMsg);
    }

    // 3. Keep the last 10 messages
    int startIdx = qMax(1, messages.size() - 10);
    for (int i = startIdx; i < messages.size(); ++i) {
        compressed.append(messages.at(i));
    }

    return compressed;
}

} // namespace NexusAI
