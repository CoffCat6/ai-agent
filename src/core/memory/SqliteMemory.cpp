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
#include <cstring>

namespace NexusAI {

namespace {

// Batch 21 (B4): row ceiling for the unindexed LIKE fallback scan. With a
// local desktop DB below this size a '%kw%' scan stays fast enough for the
// GUI thread; larger libraries must use the trigram FTS path (3+ chars).
constexpr qint64 kLikeScanMaxMessages = 50000;

// Human-friendly relative timestamp for the session list. UI-visible copy
// (P3-9): routed through SqliteMemory::tr() so it lands in the .ts file.
// "MM-dd" is a date-format string, not user-visible copy - kept untranslated.
QString relativeTime(qint64 timestamp) {
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(timestamp);
    const qint64 secs = t.secsTo(now);
    if (secs < 60) return SqliteMemory::tr("刚刚");
    if (secs < 3600) return SqliteMemory::tr("%1 分钟前").arg(secs / 60);
    if (secs < 86400) return SqliteMemory::tr("%1 小时前").arg(secs / 3600);
    if (secs < 172800) return SqliteMemory::tr("昨天");
    return t.toString(QStringLiteral("MM-dd"));
}

// Schema statements must not fail silently: e.g. a SQLite build without
// FTS5 would otherwise disable full-text search without a trace.
bool execDdl(QSqlQuery& query, const QString& sql, const char* what) {
    if (!query.exec(sql)) {
        qWarning() << "Failed to" << what << ":" << query.lastError().text();
        return false;
    }
    return true;
}

} // namespace

SqliteMemory::SqliteMemory(const QString& dbPath) : m_dbPath(dbPath) {
    if (m_dbPath.isEmpty()) {
        QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dataDir);
        m_dbPath = dataDir + "/nexusai_memory.db";
    }

    // Unique per-instance connection name; stored in m_connName so the
    // destructor can removeDatabase it (MEM-702) - an unstored name leaked
    // the connection for the lifetime of the process.
    m_connName = QStringLiteral("nexus_memory_%1")
                     .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connName);
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        qWarning() << "Failed to open memory database:" << m_db.lastError().text();
        return;
    }

    // Batch 29: parallel session tabs each own a connection to the same
    // database file; the wait bounds concurrent-write failures with
    // SQLITE_BUSY instead of failing the write outright.
    QSqlQuery busy(m_db);
    busy.exec("PRAGMA busy_timeout = 5000");

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
    migrateKnowledgeSchema();
    migrateMessagesFts();
}

// Batch 17 (FEAT-1701): messages FTS5 index for cross-session search.
// Tokenizer choice from the recorded probe (SQLite 3.49.1, this repo's
// bundled Qt): the default unicode61 tokenizer cannot substring-match CJK
// text (a whole sentence becomes one token), while trigram answers
// 3+ character CJK/ASCII substring queries. Keywords shorter than 3 chars
// fall back to LIKE in searchMessages(). Idempotent self-heal (discipline
// #8): everything is IF NOT EXISTS, and the backfill covers rows written
// before this migration so old databases upgrade without a version bump.
void SqliteMemory::migrateMessagesFts() {
    QSqlQuery query(m_db);
    execDdl(query, R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS messages_fts USING fts5(
            content,
            tokenize='trigram'
        )
    )", "create messages_fts table");
    execDdl(query, R"(
        CREATE TRIGGER IF NOT EXISTS messages_fts_ai AFTER INSERT ON messages BEGIN
            INSERT INTO messages_fts(rowid, content) VALUES (new.id, new.content);
        END
    )", "create messages_fts insert trigger");
    execDdl(query, R"(
        CREATE TRIGGER IF NOT EXISTS messages_fts_ad AFTER DELETE ON messages BEGIN
            DELETE FROM messages_fts WHERE rowid = old.id;
        END
    )", "create messages_fts delete trigger");
    // Backfill rows that predate this migration (idempotent: only rows the
    // index does not know about). Writes into messages_fts directly, so the
    // messages INSERT trigger cannot double-fire.
    execDdl(query, R"(
        INSERT INTO messages_fts(rowid, content)
            SELECT id, content FROM messages
            WHERE id NOT IN (SELECT rowid FROM messages_fts)
    )", "backfill messages_fts from messages");
}

void SqliteMemory::createTables() {
    QSqlQuery query(m_db);

    // Messages table (workspace_id: forward-compatibility reservation,
    // Batch 7 MEM-704 - always 'local' until multi-tenant support)
    execDdl(query, R"(
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            role TEXT NOT NULL,
            content TEXT NOT NULL,
            timestamp INTEGER NOT NULL,
            metadata TEXT,
            workspace_id TEXT NOT NULL DEFAULT 'local'
        )
    )", "create messages table");

    // Episodic memory table with FTS5
    execDdl(query, R"(
        CREATE TABLE IF NOT EXISTS episodic (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            summary TEXT NOT NULL,
            tags TEXT,
            embedding BLOB,
            timestamp INTEGER NOT NULL
        )
    )", "create episodic table");

    // FTS5 virtual table for full-text search
    execDdl(query, R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS episodic_fts USING fts5(
            summary, tags, content='episodic', content_rowid='id'
        )
    )", "create episodic_fts table");

    // Triggers to keep FTS in sync
    execDdl(query, R"(
        CREATE TRIGGER IF NOT EXISTS episodic_ai AFTER INSERT ON episodic BEGIN
            INSERT INTO episodic_fts(rowid, summary, tags)
            VALUES (new.id, new.summary, new.tags);
        END
    )", "create episodic_ai trigger");

    execDdl(query, R"(
        CREATE TRIGGER IF NOT EXISTS episodic_ad AFTER DELETE ON episodic BEGIN
            INSERT INTO episodic_fts(episodic_fts, rowid, summary, tags)
            VALUES('delete', old.id, old.summary, old.tags);
        END
    )", "create episodic_ad trigger");

    // Sessions table for the sidebar history
    execDdl(query, R"(
        CREATE TABLE IF NOT EXISTS sessions (
            id TEXT PRIMARY KEY,
            title TEXT NOT NULL,
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL
        )
    )", "create sessions table");

    // Audit trail for tool activity
    execDdl(query, R"(
        CREATE TABLE IF NOT EXISTS audit_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            event TEXT NOT NULL,
            tool_name TEXT NOT NULL,
            arguments TEXT,
            success INTEGER NOT NULL,
            detail TEXT,
            timestamp INTEGER NOT NULL,
            workspace_id TEXT NOT NULL DEFAULT 'local'
        )
    )", "create audit_log table");
    execDdl(query, "CREATE INDEX IF NOT EXISTS idx_audit_ts ON audit_log(timestamp)",
            "create audit_log index");

    // Knowledge-base documents (imported text, restored at startup)
    execDdl(query, R"(
        CREATE TABLE IF NOT EXISTS knowledge_documents (
            name TEXT PRIMARY KEY,
            content TEXT NOT NULL,
            imported_at INTEGER NOT NULL
        )
    )", "create knowledge_documents table");

    // Trace view nodes (thinking / tool calls / results) per session, so
    // switching conversations restores the full visual timeline
    execDdl(query, R"(
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
            workspace_id TEXT NOT NULL DEFAULT 'local',
            PRIMARY KEY (session_id, node_id)
        )
    )", "create trace_nodes table");
    execDdl(query, "CREATE INDEX IF NOT EXISTS idx_trace_nodes_session "
                   "ON trace_nodes(session_id, node_order)",
            "create trace_nodes index");

    // Cross-session usage trail (user operations / network outcomes). New
    // table — no migration needed; retention is enforced by pruneUsage.
    execDdl(query, R"(
        CREATE TABLE IF NOT EXISTS usage_log (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            ts INTEGER NOT NULL,
            category TEXT NOT NULL,
            action TEXT NOT NULL,
            target TEXT NOT NULL,
            request_id INTEGER NOT NULL DEFAULT 0,
            detail TEXT NOT NULL
        )
    )", "create usage_log table");
    execDdl(query, "CREATE INDEX IF NOT EXISTS idx_usage_ts ON usage_log(ts)",
            "create usage_log index");
}

// Schema v1→v2 (Batch 6): documents gain content-hash / embedding-model
// columns and a chunk table stores the embedded vectors. Old rows keep
// working: empty hash/model marks them stale, so the first startup after
// the upgrade re-embeds them in the background (per the audited migration
// plan) and the new layout is written back transparently.
void SqliteMemory::migrateKnowledgeSchema() {
    QSqlQuery pragma(m_db);
    QStringList columns;
    if (pragma.exec("PRAGMA table_info(knowledge_documents)")) {
        while (pragma.next())
            columns << pragma.value(1).toString();
    }

    if (columns.isEmpty()) return; // table not created (fresh db: v2 DDL handles it)
    QSqlQuery query(m_db);
    if (!columns.contains("content_hash"))
        execDdl(query, "ALTER TABLE knowledge_documents "
                       "ADD COLUMN content_hash TEXT NOT NULL DEFAULT ''",
                "add knowledge_documents.content_hash");
    if (!columns.contains("embedding_model"))
        execDdl(query, "ALTER TABLE knowledge_documents "
                       "ADD COLUMN embedding_model TEXT NOT NULL DEFAULT ''",
                "add knowledge_documents.embedding_model");

    // OBS-1102: observability columns on audit_log (Batch 11)
    {
        QSqlQuery auditColumns(m_db);
        QStringList auditNames;
        if (auditColumns.exec("PRAGMA table_info(audit_log)")) {
            while (auditColumns.next())
                auditNames << auditColumns.value(1).toString();
        }
        if (!auditNames.isEmpty()) {
            QSqlQuery alter(m_db);
            const QVector<QPair<const char*, const char*>> auditCols = {
                {"request_id", "INTEGER NOT NULL DEFAULT 0"},
                {"latency_ms", "INTEGER NOT NULL DEFAULT -1"},
                {"tokens", "INTEGER NOT NULL DEFAULT 0"},
                {"model", "TEXT NOT NULL DEFAULT ''"},
            };
            for (const auto& col : auditCols) {
                if (!auditNames.contains(col.first)) {
                    execDdl(alter, QString("ALTER TABLE audit_log ADD COLUMN %1 %2")
                                      .arg(col.first, col.second),
                            qPrintable(QString("add audit_log.%1").arg(col.first)));
                }
            }
        }
    }

    // MEM-704: workspace_id reservation on the three session tables
    for (const QString& table : { QStringLiteral("messages"),
                                  QStringLiteral("audit_log"),
                                  QStringLiteral("trace_nodes") }) {
        QSqlQuery wsColumns(m_db);
        QStringList wsNames;
        if (wsColumns.exec(QString("PRAGMA table_info(%1)").arg(table))) {
            while (wsColumns.next())
                wsNames << wsColumns.value(1).toString();
        }
        if (!wsNames.isEmpty() && !wsNames.contains("workspace_id")) {
            QSqlQuery alter(m_db);
            execDdl(alter, QString("ALTER TABLE %1 ADD COLUMN workspace_id "
                                   "TEXT NOT NULL DEFAULT 'local'").arg(table),
                    qPrintable(QString("add %1.workspace_id").arg(table)));
        }
    }

    execDdl(query, R"(
        CREATE TABLE IF NOT EXISTS knowledge_chunks (
            doc_name TEXT NOT NULL,
            chunk_index INTEGER NOT NULL,
            chunk_total INTEGER NOT NULL,
            text TEXT NOT NULL,
            embedding BLOB NOT NULL,
            dim INTEGER NOT NULL,
            pseudo INTEGER NOT NULL,
            PRIMARY KEY (doc_name, chunk_index)
        )
    )", "create knowledge_chunks table");
}

// ── Knowledge-base persistence (v2) ──────────────────────

namespace {

QByteArray embeddingToBlob(const QVector<float>& vec) {
    QByteArray blob(reinterpret_cast<const char*>(vec.constData()),
                    int(vec.size() * sizeof(float)));
    return blob;
}

QVector<float> embeddingFromBlob(const QByteArray& blob) {
    QVector<float> vec(blob.size() / int(sizeof(float)));
    if (!vec.isEmpty())
        std::memcpy(vec.data(), blob.constData(), size_t(vec.size()) * sizeof(float));
    return vec;
}

} // namespace

void SqliteMemory::upsertKnowledgeDoc(const QString& name, const QString& content,
                                      const QString& contentHash,
                                      const QString& embeddingModel) {
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO knowledge_documents (name, content, imported_at, "
                  "content_hash, embedding_model) VALUES (?, ?, ?, ?, ?) "
                  "ON CONFLICT(name) DO UPDATE SET content = excluded.content, "
                  "imported_at = excluded.imported_at, content_hash = excluded.content_hash, "
                  "embedding_model = excluded.embedding_model");
    query.addBindValue(name);
    query.addBindValue(content);
    query.addBindValue(QDateTime::currentDateTime().toMSecsSinceEpoch());
    query.addBindValue(contentHash);
    query.addBindValue(embeddingModel);

    if (!query.exec()) {
        qWarning() << "Failed to save knowledge document:" << query.lastError().text();
    }
}

void SqliteMemory::replaceKnowledgeChunks(const QString& name,
                                          const QVector<KnowledgeChunk>& chunks) {
    QSqlQuery query(m_db);
    if (!m_db.transaction()) {
        qWarning() << "Failed to start chunk transaction:" << query.lastError().text();
        return;
    }

    query.prepare("DELETE FROM knowledge_chunks WHERE doc_name = ?");
    query.addBindValue(name);
    if (!query.exec()) {
        qWarning() << "Failed to clear chunks:" << query.lastError().text();
        m_db.rollback();
        return;
    }

    query.prepare("INSERT INTO knowledge_chunks (doc_name, chunk_index, chunk_total, "
                  "text, embedding, dim, pseudo) VALUES (?, ?, ?, ?, ?, ?, ?)");
    for (const KnowledgeChunk& chunk : chunks) {
        query.addBindValue(name);
        query.addBindValue(chunk.index);
        query.addBindValue(chunk.total);
        query.addBindValue(chunk.text);
        query.addBindValue(embeddingToBlob(chunk.embedding));
        query.addBindValue(chunk.dim);
        query.addBindValue(chunk.pseudo ? 1 : 0);
        if (!query.exec()) {
            qWarning() << "Failed to save chunk:" << query.lastError().text();
            m_db.rollback();
            return;
        }
    }

    if (!m_db.commit()) {
        qWarning() << "Failed to commit chunks:" << query.lastError().text();
        m_db.rollback();
    }
}

QVector<KnowledgeChunk> SqliteMemory::loadKnowledgeChunks(const QString& name) const {
    QSqlQuery query(m_db);
    query.prepare("SELECT chunk_index, chunk_total, text, embedding, dim, pseudo "
                  "FROM knowledge_chunks WHERE doc_name = ? ORDER BY chunk_index ASC");
    query.addBindValue(name);

    QVector<KnowledgeChunk> chunks;
    if (query.exec()) {
        while (query.next()) {
            KnowledgeChunk chunk;
            chunk.index = query.value(0).toInt();
            chunk.total = query.value(1).toInt();
            chunk.text = query.value(2).toString();
            chunk.embedding = embeddingFromBlob(query.value(3).toByteArray());
            chunk.dim = query.value(4).toInt();
            chunk.pseudo = query.value(5).toInt() != 0;
            chunks.append(chunk);
        }
    } else {
        qWarning() << "Failed to load chunks:" << query.lastError().text();
    }
    return chunks;
}

bool SqliteMemory::removeKnowledgeDoc(const QString& name) {
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM knowledge_chunks WHERE doc_name = ?");
    query.addBindValue(name);
    if (!query.exec())
        qWarning() << "Failed to remove chunks:" << query.lastError().text();

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
    if (!query.exec("DELETE FROM knowledge_chunks")) {
        qWarning() << "Failed to clear knowledge chunks:" << query.lastError().text();
    }
    if (!query.exec("DELETE FROM knowledge_documents")) {
        qWarning() << "Failed to clear knowledge documents:" << query.lastError().text();
    }
}

QVector<SqliteMemory::KnowledgeDocRecord> SqliteMemory::loadKnowledgeDocRecords() const {
    QSqlQuery query(m_db);
    query.prepare("SELECT name, content, content_hash, embedding_model "
                  "FROM knowledge_documents ORDER BY imported_at ASC");

    QVector<KnowledgeDocRecord> records;
    if (query.exec()) {
        while (query.next()) {
            KnowledgeDocRecord rec;
            rec.name = query.value(0).toString();
            rec.content = query.value(1).toString();
            rec.contentHash = query.value(2).toString();
            rec.embeddingModel = query.value(3).toString();
            records.append(rec);
        }
    } else {
        qWarning() << "Failed to load knowledge records:" << query.lastError().text();
    }
    return records;
}

QString SqliteMemory::loadKnowledgeDoc(const QString& name) const {
    QSqlQuery query(m_db);
    query.prepare("SELECT content FROM knowledge_documents WHERE name = ?");
    query.addBindValue(name);

    if (query.exec() && query.next())
        return query.value(0).toString();
    return QString();
}

void SqliteMemory::saveMessage(const QString& sessionId, const QJsonObject& msg) {
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO messages (session_id, role, content, timestamp, metadata, "
                  "workspace_id) VALUES (?, ?, ?, ?, ?, 'local')");
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
    // Latest `limit` messages in chronological order: the inner DESC picks
    // the newest window (rowid breaks timestamp ties), the outer ASC
    // restores conversation order. A plain "ASC LIMIT ?" kept the OLDEST
    // messages and silently dropped the recent ones in long sessions.
    query.prepare("SELECT role, content, metadata, timestamp FROM ("
                  "SELECT role, content, metadata, timestamp, rowid FROM messages "
                  "WHERE session_id = ? "
                  "ORDER BY timestamp DESC, rowid DESC LIMIT ?"
                  ") ORDER BY timestamp ASC, rowid ASC");
    query.addBindValue(sessionId);
    query.addBindValue(limit);

    QJsonArray messages;
    if (query.exec()) {
        while (query.next()) {
            QJsonObject msg;
            msg["role"] = query.value(0).toString();
            msg["content"] = query.value(1).toString();
            msg["metadata"] = QJsonDocument::fromJson(query.value(2).toString().toUtf8()).object();
            // Batch 18: msec epoch for export timestamps (consumers that
            // don't need it simply ignore the extra key).
            msg["timestamp"] = query.value(3).toDouble();
            messages.append(msg);
        }
    }
    return messages;
}

// Batch 17 (FEAT-1702): cross-session keyword search, newest first. The
// trigram FTS path handles 3+ character keywords (quoted phrase → substring
// match, CJK included); shorter keywords fall back to a LIKE scan because a
// trigram index cannot answer sub-trigram queries. Snippets come from fts5
// snippet() on the FTS path and a match-centered window on the LIKE path.
QVariantList SqliteMemory::searchMessages(const QString& keyword, int limit) {
    QVariantList out;
    const QString kw = keyword.trimmed();
    if (kw.isEmpty() || limit <= 0) return out;

    QSqlQuery query(m_db);
    const bool ftsPath = kw.length() >= 3;
    if (!ftsPath) {
        // Batch 21 (B4): the LIKE '%kw%' fallback cannot use any index
        // (leading wildcard) and turns into a full table scan on the GUI
        // thread. Guard it with a message-count ceiling: below it the scan on
        // a local desktop DB stays in the tens of ms; above it, hand the user
        // a notice asking for a 3+ character keyword (which uses the trigram
        // FTS index) instead of stalling the UI. COUNT(*) is cheap (SQLite
        // keeps statistics; worst case a quick index/table count).
        QSqlQuery countQuery(m_db);
        if (countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM messages"))
                && countQuery.next()
                && countQuery.value(0).toLongLong() > kLikeScanMaxMessages) {
            qWarning() << "searchMessages: LIKE fallback suppressed - message"
                          " table exceeds" << kLikeScanMaxMessages << "rows";
            QVariantMap notice;
            notice.insert(QStringLiteral("sessionId"),
                          QStringLiteral("__search_notice__"));
            notice.insert(QStringLiteral("notice"),
                SqliteMemory::tr("消息库超过 %1 条：少于 3 个字符的关键词只能全表扫描，"
                                 "请输入至少 3 个字符以使用全文索引。")
                    .arg(kLikeScanMaxMessages));
            out.append(notice);
            return out;
        }
    }
    // Batch 21 (A3): results are grouped by session for the Sidebar section
    // headers (section.property = "sessionId"), so identical session ids must
    // be CONTIGUOUS. Order by the group's newest hit first, then by timestamp
    // inside the group - a plain timestamp DESC interleaved sessions and made
    // the same group header render repeatedly. The window functions live in
    // an OUTER query: FTS5's snippet() is an auxiliary function bound to the
    // virtual-table scan and is illegal once a window function forces
    // materialization at the same level ("unable to use function snippet in
    // the requested context"), so the inner query resolves snippet() to a
    // plain column first.
    const QString groupOrder = QStringLiteral(
        "ORDER BY MAX(timestamp) OVER (PARTITION BY session_id) DESC, "
        "MAX(id) OVER (PARTITION BY session_id) DESC, "
        "timestamp DESC, id DESC");
    if (ftsPath) {
        QString match = kw;
        match.replace(QLatin1String("\""), QLatin1String("\"\""));
        query.prepare(QStringLiteral(
            "SELECT id, session_id, role, timestamp, snip, title FROM ("
            "SELECT m.id AS id, m.session_id AS session_id, m.role AS role, "
            "m.timestamp AS timestamp, "
            "snippet(messages_fts, 0, '[', ']', '…', 16) AS snip, s.title AS title "
            "FROM messages_fts JOIN messages m ON m.id = messages_fts.rowid "
            "LEFT JOIN sessions s ON s.id = m.session_id "
            "WHERE messages_fts MATCH ?"
            ") %1 LIMIT ?").arg(groupOrder));
        query.addBindValue(QStringLiteral("\"%1\"").arg(match));
    } else {
        QString like = kw;
        like.replace(QLatin1String("\\"), QLatin1String("\\\\"))
            .replace(QLatin1String("%"), QLatin1String("\\%"))
            .replace(QLatin1String("_"), QLatin1String("\\_"));
        query.prepare(QStringLiteral(
            "SELECT id, session_id, role, timestamp, content AS snip, title FROM ("
            "SELECT m.id AS id, m.session_id AS session_id, m.role AS role, "
            "m.timestamp AS timestamp, m.content AS content, s.title AS title "
            "FROM messages m LEFT JOIN sessions s ON s.id = m.session_id "
            "WHERE m.content LIKE ? ESCAPE '\\'"
            ") %1 LIMIT ?").arg(groupOrder));
        query.addBindValue(QStringLiteral("%%1%").arg(like));
    }
    query.addBindValue(limit);
    if (!query.exec()) {
        qWarning() << "searchMessages failed:" << query.lastError().text();
        return out;
    }
    while (query.next()) {
        QVariantMap row;
        row.insert(QStringLiteral("messageId"), query.value(0));
        row.insert(QStringLiteral("sessionId"), query.value(1));
        const QString role = query.value(2).toString();
        const qint64 ts = query.value(3).toLongLong();
        QString snippet = query.value(4).toString();
        if (!ftsPath) {
            // LIKE path: center a window on the first (case-insensitive) hit.
            const int hit = snippet.indexOf(kw, 0, Qt::CaseInsensitive);
            const int window = 48;
            if (hit > window) {
                const int start = hit - window / 2;
                snippet = QStringLiteral("…") + snippet.mid(start, window + kw.length());
            }
            if (snippet.length() > window * 2 + kw.length())
                snippet += QStringLiteral("…");
        }
        row.insert(QStringLiteral("snippet"), snippet);
        row.insert(QStringLiteral("role"), role);
        row.insert(QStringLiteral("timestamp"), ts);
        row.insert(QStringLiteral("sessionTitle"), query.value(5));
        out.append(row);
    }
    return out;
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
    if (!query.exec())
        qWarning() << "Failed to clear messages:" << query.lastError().text();

    query.prepare("DELETE FROM episodic WHERE session_id = ?");
    query.addBindValue(sessionId);
    if (!query.exec())
        qWarning() << "Failed to clear episodic memory:" << query.lastError().text();
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
    const auto execDelete = [&](const char* what) {
        if (!query.exec())
            qWarning() << "Failed to" << what << ":" << query.lastError().text();
    };

    query.prepare("DELETE FROM messages WHERE session_id = ?");
    query.addBindValue(sessionId);
    execDelete("delete session messages");

    query.prepare("DELETE FROM episodic WHERE session_id = ?");
    query.addBindValue(sessionId);
    execDelete("delete session episodic memory");

    // FTS shadow table keeps itself in sync via the delete trigger
    query.prepare("DELETE FROM sessions WHERE id = ?");
    query.addBindValue(sessionId);
    execDelete("delete session record");

    query.prepare("DELETE FROM trace_nodes WHERE session_id = ?");
    query.addBindValue(sessionId);
    execDelete("delete session trace nodes");
}

// ── Trace view persistence ───────────────────────────────

void SqliteMemory::saveTraceNode(const QString& sessionId, const QJsonObject& node, int order) {
    QSqlQuery query(m_db);
    // Upsert: position (node_order) is fixed by the first insert; later
    // saves of the same node id only refresh content/status.
    query.prepare("INSERT INTO trace_nodes (session_id, node_id, node_order, type, title, "
                  "status, args, result, timestamp, workspace_id) "
                  "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 'local') "
                  "ON CONFLICT(session_id, node_id) DO UPDATE SET type = excluded.type, "
                  "title = excluded.title, status = excluded.status, args = excluded.args, "
                  "result = excluded.result, timestamp = excluded.timestamp");
    // Batch 18 review: every text column is NOT NULL. An absent JSON key
    // yields a null QString, and a null QString binds as SQL NULL — the same
    // silent-drop trap as usage_log.detail (Batch 11). Normalize the bind.
    const auto nz = [](const QJsonObject& object, const char* key) {
        const QString value = object.value(key).toString();
        return value.isNull() ? QString(QStringLiteral("")) : value;
    };
    query.addBindValue(sessionId);
    query.addBindValue(nz(node, "id"));
    query.addBindValue(order);
    query.addBindValue(nz(node, "type"));
    query.addBindValue(nz(node, "title"));
    query.addBindValue(nz(node, "status"));
    query.addBindValue(nz(node, "args"));
    query.addBindValue(nz(node, "result"));
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

// ── Usage trail ─────────────────────────────────────────

void SqliteMemory::addUsageEvent(const QString& category, const QString& action,
                                 const QString& target, quint64 requestId,
                                 const QString& detail) {
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO usage_log (ts, category, action, target, "
                  "request_id, detail) VALUES (?, ?, ?, ?, ?, ?)");
    query.addBindValue(QDateTime::currentDateTime().toMSecsSinceEpoch());
    query.addBindValue(category);
    query.addBindValue(action);
    query.addBindValue(target);
    query.addBindValue(static_cast<qint64>(requestId));
    // Empty QString binds as SQL NULL; usage_log.detail is NOT NULL
    query.addBindValue(detail.isNull() ? QStringLiteral("") : detail);
    if (!query.exec())
        qWarning() << "Failed to write usage event:" << query.lastError().text();
}

QJsonArray SqliteMemory::loadUsageEvents(int limit) const {
    QSqlQuery query(m_db);
    query.prepare("SELECT ts, category, action, target, request_id, detail "
                  "FROM usage_log ORDER BY id DESC LIMIT ?");
    query.addBindValue(limit);

    QJsonArray entries;
    if (query.exec()) {
        while (query.next()) {
            QJsonObject e;
            e["timestamp"] = query.value(0).toLongLong();
            e["category"] = query.value(1).toString();
            e["action"] = query.value(2).toString();
            e["target"] = query.value(3).toString();
            e["requestId"] = static_cast<double>(query.value(4).toLongLong());
            e["detail"] = query.value(5).toString();
            entries.append(e);
        }
    } else {
        qWarning() << "Failed to read usage events:" << query.lastError().text();
    }
    return entries;
}

int SqliteMemory::pruneUsage(int keepDays, int maxRows) {
    QSqlQuery query(m_db);
    int removed = 0;
    if (keepDays > 0) {
        const qint64 cutoff = QDateTime::currentMSecsSinceEpoch()
                            - static_cast<qint64>(keepDays) * 86400 * 1000;
        query.prepare("DELETE FROM usage_log WHERE ts < ?");
        query.addBindValue(cutoff);
        if (query.exec()) removed += query.numRowsAffected();
    }
    if (maxRows > 0) {
        query.prepare("DELETE FROM usage_log WHERE id NOT IN "
                      "(SELECT id FROM usage_log ORDER BY id DESC LIMIT ?)");
        query.addBindValue(maxRows);
        if (query.exec()) removed += query.numRowsAffected();
    }
    return removed;
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
                            bool success, const QString& detail,
                            quint64 requestId, qint64 latencyMs,
                            int tokens, const QString& model) {
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO audit_log (session_id, event, tool_name, arguments, "
                  "success, detail, timestamp, workspace_id, request_id, latency_ms, "
                  "tokens, model) VALUES (?, ?, ?, ?, ?, ?, ?, 'local', ?, ?, ?, ?)");
    query.addBindValue(sessionId);
    query.addBindValue(event);
    query.addBindValue(toolName.isNull() ? QStringLiteral("") : toolName);
    query.addBindValue(arguments.isNull() ? QStringLiteral("") : arguments);
    query.addBindValue(success ? 1 : 0);
    query.addBindValue(detail.isNull() ? QStringLiteral("") : detail);
    query.addBindValue(QDateTime::currentDateTime().toMSecsSinceEpoch());
    query.addBindValue(static_cast<qint64>(requestId));
    query.addBindValue(static_cast<qint64>(latencyMs));
    query.addBindValue(tokens);
    query.addBindValue(model.isNull() ? QStringLiteral("") : model);

    if (!query.exec()) {
        qWarning() << "Failed to write audit entry:" << query.lastError().text();
    }
}

QJsonArray SqliteMemory::auditLog(int limit) {
    QSqlQuery query(m_db);
    query.prepare("SELECT session_id, event, tool_name, arguments, success, detail, "
                  "timestamp, request_id, latency_ms, tokens, model FROM audit_log "
                  "ORDER BY timestamp DESC, id DESC LIMIT ?");
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
            entry["requestId"] = static_cast<double>(query.value(7).toLongLong());
            entry["latencyMs"] = query.value(8).toLongLong();
            entry["tokens"] = query.value(9).toInt();
            entry["model"] = query.value(10).toString();
            entries.append(entry);
        }
    } else {
        qWarning() << "Failed to read audit log:" << query.lastError().text();
    }
    return entries;
}

} // namespace NexusAI
