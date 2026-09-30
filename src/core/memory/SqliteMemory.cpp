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
#include <QDate>
#include <QTime>
#include <cstring>

#include "core/utils/PricingCalculator.h"

namespace NexusAI {

namespace {

// Batch 21 (B4): row ceiling for the unindexed LIKE fallback scan. With a
// local desktop DB below this size a '%kw%' scan stays fast enough for the
// GUI thread; larger libraries must use the trigram FTS path (3+ chars).
constexpr qint64 kLikeScanMaxMessages = 50000;

// docs/16 §6.1: 价格单位为"分/1M token"，1M 基准用于整数计费换算。
constexpr qint64 kPerMillion = 1000000;

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
    // docs/17 A0: sessions.project_id 列补齐（projects 表在 createTables 里）。
    migrateSessionProjectColumn();
    migrateKnowledgeSchema();
    migrateMessagesFts();
    migrateEpisodicFts();
    // docs/16 §4.2/§6.2: 预置公开定价（不覆盖用户自定义），并载入
    // PricingCalculator 进程级缓存，供 costStats 等聚合查询使用。
    seedModelPricing();
    PricingCalculator::refreshCache(m_db);
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

// L3: upgrade episodic_fts from the default unicode61 tokenizer to trigram.
// Idempotent: no-op when the table already uses trigram (fresh databases get
// it from createTables; this handles old databases upgraded in place). The
// table is an external-content FTS5 (content='episodic'), so after recreating
// it we issue the 'rebuild' command to repopulate from the episodic table.
void SqliteMemory::migrateEpisodicFts() {
    QSqlQuery check(m_db);
    check.prepare("SELECT sql FROM sqlite_master "
                  "WHERE type='table' AND name='episodic_fts'");
    if (!check.exec() || !check.next()) return; // table absent — createTables owns it
    const QString sql = check.value(0).toString();
    if (sql.contains(QStringLiteral("tokenize='trigram'"), Qt::CaseInsensitive))
        return; // already upgraded

    // Old table without trigram: drop triggers first (they reference
    // episodic_fts) to avoid dangling-trigger errors on the next episodic
    // INSERT, then drop and recreate the virtual table + triggers, and
    // finally repopulate from the external content table.
    QSqlQuery query(m_db);
    execDdl(query, "DROP TRIGGER IF EXISTS episodic_ai", "drop old episodic_ai");
    execDdl(query, "DROP TRIGGER IF EXISTS episodic_ad", "drop old episodic_ad");
    execDdl(query, "DROP TABLE IF EXISTS episodic_fts", "drop old episodic_fts");
    execDdl(query, R"(
        CREATE VIRTUAL TABLE episodic_fts USING fts5(
            summary, tags, content='episodic', content_rowid='id',
            tokenize='trigram'
        )
    )", "recreate episodic_fts with trigram");
    execDdl(query, R"(
        CREATE TRIGGER episodic_ai AFTER INSERT ON episodic BEGIN
            INSERT INTO episodic_fts(rowid, summary, tags)
            VALUES (new.id, new.summary, new.tags);
        END
    )", "recreate episodic_ai trigger");
    execDdl(query, R"(
        CREATE TRIGGER episodic_ad AFTER DELETE ON episodic BEGIN
            INSERT INTO episodic_fts(episodic_fts, rowid, summary, tags)
            VALUES('delete', old.id, old.summary, old.tags);
        END
    )", "recreate episodic_ad trigger");
    execDdl(query, "INSERT INTO episodic_fts(episodic_fts) VALUES('rebuild')",
            "rebuild episodic_fts index");
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

    // FTS5 virtual table for full-text search. tokenize='trigram' (L3):
    // the default unicode61 tokenizer cannot substring-match CJK text — a
    // whole Chinese sentence collapses into one token. trigram answers 3+
    // character CJK/ASCII substring queries; shorter queries fall back to
    // LIKE in searchEpisodic().
    execDdl(query, R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS episodic_fts USING fts5(
            summary, tags, content='episodic', content_rowid='id',
            tokenize='trigram'
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

    // docs/17 A0: 项目级会话——导入的文件夹作为一个项目。
    execDdl(query, R"(
        CREATE TABLE IF NOT EXISTS projects (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            root_path TEXT NOT NULL,
            created_at INTEGER NOT NULL
        )
    )", "create projects table");

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

    // docs/16 §4.1: token_usage 明细表 —— 每次 LLM 请求一行，3 方向 × 7 来源
    // 的分类 token + 费用 + 延迟。audit_log.tokens 保持不变（向后兼容），
    // 本表是新增的精确明细，tokenStatsDetailed 等从此聚合。
    execDdl(query, R"(
        CREATE TABLE IF NOT EXISTS token_usage (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            request_id      INTEGER NOT NULL,
            session_id      TEXT NOT NULL,
            model           TEXT NOT NULL,
            timestamp       INTEGER NOT NULL,
            input_tokens        INTEGER NOT NULL DEFAULT 0,
            output_tokens       INTEGER NOT NULL DEFAULT 0,
            cache_hit_tokens    INTEGER NOT NULL DEFAULT 0,
            input_system_prompt INTEGER NOT NULL DEFAULT 0,
            input_conversation  INTEGER NOT NULL DEFAULT 0,
            input_tool_call     INTEGER NOT NULL DEFAULT 0,
            input_mcp           INTEGER NOT NULL DEFAULT 0,
            input_skill         INTEGER NOT NULL DEFAULT 0,
            input_image         INTEGER NOT NULL DEFAULT 0,
            input_other         INTEGER NOT NULL DEFAULT 0,
            output_conversation INTEGER NOT NULL DEFAULT 0,
            output_tool_call    INTEGER NOT NULL DEFAULT 0,
            output_mcp          INTEGER NOT NULL DEFAULT 0,
            output_skill        INTEGER NOT NULL DEFAULT 0,
            output_other        INTEGER NOT NULL DEFAULT 0,
            cache_hit_system_prompt INTEGER NOT NULL DEFAULT 0,
            cache_hit_conversation  INTEGER NOT NULL DEFAULT 0,
            cache_hit_tool_call     INTEGER NOT NULL DEFAULT 0,
            cost_cents       INTEGER NOT NULL DEFAULT 0,
            latency_ms       INTEGER NOT NULL DEFAULT -1,
            workspace_id     TEXT NOT NULL DEFAULT 'local'
        )
    )", "create token_usage table");
    execDdl(query, "CREATE INDEX IF NOT EXISTS idx_token_usage_ts "
                   "ON token_usage(timestamp)",
            "create token_usage timestamp index");
    execDdl(query, "CREATE INDEX IF NOT EXISTS idx_token_usage_model "
                   "ON token_usage(model)",
            "create token_usage model index");
    execDdl(query, "CREATE INDEX IF NOT EXISTS idx_token_usage_session "
                   "ON token_usage(session_id)",
            "create token_usage session index");

    // docs/16 §4.2: model_pricing 定价表 —— 每 1M token 价格（分）。
    // 预置数据由 seedModelPricing() 在 initDatabase 末尾写入。
    execDdl(query, R"(
        CREATE TABLE IF NOT EXISTS model_pricing (
            model                   TEXT PRIMARY KEY,
            input_price_cents       INTEGER NOT NULL,
            output_price_cents      INTEGER NOT NULL,
            cache_hit_price_cents   INTEGER NOT NULL DEFAULT 0,
            effective_date          INTEGER NOT NULL,
            notes                   TEXT
        )
    )", "create model_pricing table");
}

// Schema v1→v2 (Batch 6): documents gain content-hash / embedding-model
// columns and a chunk table stores the embedded vectors. Old rows keep
// working: empty hash/model marks them stale, so the first startup after
// the upgrade re-embeds them in the background (per the audited migration
// plan) and the new layout is written back transparently.
// docs/17 A0: 老库补齐 sessions.project_id 列（PRAGMA 检查 + 受控 ALTER，
// 幂等——重复调用在列已存在时为 no-op）。
void SqliteMemory::migrateSessionProjectColumn() {
    QSqlQuery q(m_db);
    if (!q.exec("PRAGMA table_info(sessions)")) {
        qWarning() << "PRAGMA table_info(sessions) failed:"
                   << q.lastError().text();
        return;
    }
    bool hasColumn = false;
    while (q.next()) {
        if (q.value(1).toString() == QLatin1String("project_id")) {
            hasColumn = true;
            break;
        }
    }
    if (!hasColumn)
        execDdl(q, "ALTER TABLE sessions ADD COLUMN project_id TEXT",
                "add sessions.project_id column");
}

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
    // Batch 30 (F6): attached images ride in the metadata JSON column (no
    // schema migration); loadMessages promotes them back to the top level.
    QJsonObject metadata = msg.value("metadata").toObject();
    const QJsonArray images = msg.value("images").toArray();
    if (!images.isEmpty())
        metadata["images"] = images;
    const QJsonArray files = msg.value("files").toArray();
    if (!files.isEmpty())
        metadata["files"] = files;
    query.addBindValue(QJsonDocument(metadata).toJson(QJsonDocument::Compact));

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
            // Batch 30 (F6): promote attached images back to the top level so
            // the provider converters and the trace rebuild see them.
            const QJsonObject metadata = msg.value("metadata").toObject();
            if (metadata.contains("images"))
                msg["images"] = metadata.value("images");
            if (metadata.contains("files"))
                msg["files"] = metadata.value("files");
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
    QJsonArray results;
    const QString kw = query.trimmed();
    if (kw.isEmpty() || limit <= 0) return results;

    QSqlQuery sqlQuery(m_db);
    const bool ftsPath = kw.length() >= 3;
    if (ftsPath) {
        // Trigram FTS path (3+ chars). Wrap the keyword in double quotes
        // (doubling embedded quotes) so FTS5 treats it as a phrase, avoiding
        // syntax errors on special characters like '*' or '('.
        QString match = kw;
        match.replace(QLatin1String("\""), QLatin1String("\"\""));
        sqlQuery.prepare(QStringLiteral(
            "SELECT e.id, e.session_id, e.summary, e.tags, e.timestamp "
            "FROM episodic_fts f JOIN episodic e ON f.rowid = e.id "
            "WHERE episodic_fts MATCH ? ORDER BY rank LIMIT ?"));
        sqlQuery.addBindValue(QStringLiteral("\"%1\"").arg(match));
    } else {
        // Sub-trigram query: trigram cannot answer, fall back to LIKE on
        // summary + tags. Escape LIKE wildcards so user input is literal.
        QString like = kw;
        like.replace(QLatin1String("\\"), QLatin1String("\\\\"))
            .replace(QLatin1String("%"), QLatin1String("\\%"))
            .replace(QLatin1String("_"), QLatin1String("\\_"));
        sqlQuery.prepare(QStringLiteral(
            "SELECT e.id, e.session_id, e.summary, e.tags, e.timestamp "
            "FROM episodic e "
            "WHERE e.summary LIKE ? ESCAPE '\\' OR e.tags LIKE ? ESCAPE '\\' "
            "ORDER BY e.timestamp DESC LIMIT ?"));
        sqlQuery.addBindValue(QStringLiteral("%%1%").arg(like));
        sqlQuery.addBindValue(QStringLiteral("%%1%").arg(like));
    }
    sqlQuery.addBindValue(limit);

    if (!sqlQuery.exec()) {
        qWarning() << "searchEpisodic failed:" << sqlQuery.lastError().text();
        return results;
    }
    while (sqlQuery.next()) {
        QJsonObject episode;
        episode["id"] = sqlQuery.value(0).toInt();
        episode["session_id"] = sqlQuery.value(1).toString();
        episode["summary"] = sqlQuery.value(2).toString();
        episode["tags"] = sqlQuery.value(3).toString();
        episode["timestamp"] = sqlQuery.value(4).toLongLong();
        results.append(episode);
    }
    return results;
}

// M1: enumerate every stored episode, newest first. searchEpisodic cannot do
// this (it needs a non-empty keyword), and the memory-management UI needs the
// full list to browse and forget entries.
QJsonArray SqliteMemory::listEpisodic(int limit) {
    QJsonArray results;
    if (limit <= 0) return results;
    QSqlQuery query(m_db);
    query.prepare("SELECT id, session_id, summary, tags, timestamp FROM episodic "
                  "ORDER BY timestamp DESC, id DESC LIMIT ?");
    query.addBindValue(limit);
    if (!query.exec()) {
        qWarning() << "listEpisodic failed:" << query.lastError().text();
        return results;
    }
    while (query.next()) {
        QJsonObject episode;
        episode["id"] = query.value(0).toInt();
        episode["session_id"] = query.value(1).toString();
        episode["summary"] = query.value(2).toString();
        episode["tags"] = query.value(3).toString();
        episode["timestamp"] = query.value(4).toLongLong();
        results.append(episode);
    }
    return results;
}

// M1: forget one episode. The episodic_ad FTS trigger keeps the index in sync.
bool SqliteMemory::deleteEpisodic(qint64 id) {
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM episodic WHERE id = ?");
    query.addBindValue(id);
    if (!query.exec()) {
        qWarning() << "deleteEpisodic failed:" << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() > 0;
}

// M1: wipe the whole episodic store. Returns the number of rows removed.
int SqliteMemory::deleteAllEpisodic() {
    QSqlQuery query(m_db);
    if (!query.exec(QStringLiteral("DELETE FROM episodic"))) {
        qWarning() << "deleteAllEpisodic failed:" << query.lastError().text();
        return 0;
    }
    return qMax(0, query.numRowsAffected());
}

// Token/cost statistics (M1): audit_log's task_complete rows are the only
// place real token counts land (llm_request writes 0 - the count is unknown
// at request start). Aggregate over the trailing window by model and by
// local day.
QVariantMap SqliteMemory::tokenStats(int recentDays) {
    QVariantMap out;
    out["totalCalls"] = 0;
    out["totalTokens"] = 0;
    out["avgLatencyMs"] = 0;
    out["byModel"] = QVariantList{};
    out["byDay"] = QVariantList{};
    if (recentDays <= 0) recentDays = 30;

    const qint64 cutoff = QDateTime::currentDateTime().addDays(-recentDays)
                              .toMSecsSinceEpoch();

    QSqlQuery total(m_db);
    total.prepare(QStringLiteral(
        "SELECT COUNT(*), COALESCE(SUM(tokens),0), COALESCE(AVG(latency_ms),0) "
        "FROM audit_log WHERE event='task_complete' AND timestamp >= ?"));
    total.addBindValue(cutoff);
    if (total.exec() && total.next()) {
        out["totalCalls"] = total.value(0).toInt();
        out["totalTokens"] = total.value(1).toLongLong();
        out["avgLatencyMs"] = total.value(2).toInt();
    } else {
        qWarning() << "tokenStats totals failed:" << total.lastError().text();
    }

    QVariantList byModel;
    QSqlQuery modelQ(m_db);
    modelQ.prepare(QStringLiteral(
        "SELECT model, COUNT(*), COALESCE(SUM(tokens),0), "
        "CAST(COALESCE(AVG(latency_ms),0) AS INTEGER) "
        "FROM audit_log WHERE event='task_complete' AND timestamp >= ? "
        "GROUP BY model ORDER BY SUM(tokens) DESC"));
    modelQ.addBindValue(cutoff);
    if (modelQ.exec()) {
        while (modelQ.next()) {
            byModel.append(QVariantMap{
                {QStringLiteral("model"), modelQ.value(0).toString()},
                {QStringLiteral("calls"), modelQ.value(1).toInt()},
                {QStringLiteral("tokens"), modelQ.value(2).toLongLong()},
                {QStringLiteral("avgLatencyMs"), modelQ.value(3).toInt()},
            });
        }
    } else {
        qWarning() << "tokenStats byModel failed:" << modelQ.lastError().text();
    }
    out["byModel"] = byModel;

    QVariantList byDay;
    QSqlQuery dayQ(m_db);
    dayQ.prepare(QStringLiteral(
        "SELECT date(timestamp/1000, 'unixepoch', 'localtime') AS d, "
        "COUNT(*), COALESCE(SUM(tokens),0) "
        "FROM audit_log WHERE event='task_complete' AND timestamp >= ? "
        "GROUP BY d ORDER BY d DESC LIMIT 31"));
    dayQ.addBindValue(cutoff);
    if (dayQ.exec()) {
        while (dayQ.next()) {
            byDay.append(QVariantMap{
                {QStringLiteral("day"), dayQ.value(0).toString()},
                {QStringLiteral("calls"), dayQ.value(1).toInt()},
                {QStringLiteral("tokens"), dayQ.value(2).toLongLong()},
            });
        }
    } else {
        qWarning() << "tokenStats byDay failed:" << dayQ.lastError().text();
    }
    out["byDay"] = byDay;

    return out;
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
    query.prepare("SELECT id, title, updated_at, project_id FROM sessions "
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
            session["projectId"] = query.value(3).toString();
            sessions.append(session);
        }
    } else {
        qWarning() << "Failed to list sessions:" << query.lastError().text();
    }
    return sessions;
}

// ── docs/17 A0: 项目级会话 ────────────────────────────
QString SqliteMemory::createProject(const QString& name, const QString& rootPath) {
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO projects (id, name, root_path, created_at) "
              "VALUES (?, ?, ?, ?)");
    q.addBindValue(id);
    q.addBindValue(name);
    q.addBindValue(rootPath);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    if (!q.exec()) {
        qWarning() << "Failed to create project:" << q.lastError().text();
        return QString();
    }
    return id;
}

void SqliteMemory::removeProject(const QString& projectId) {
    // 会话保留：project_id 置 NULL（不级联删除）。
    QSqlQuery detach(m_db);
    detach.prepare("UPDATE sessions SET project_id = NULL WHERE project_id = ?");
    detach.addBindValue(projectId);
    if (!detach.exec())
        qWarning() << "Failed to detach project sessions:"
                   << detach.lastError().text();

    QSqlQuery q(m_db);
    q.prepare("DELETE FROM projects WHERE id = ?");
    q.addBindValue(projectId);
    if (!q.exec())
        qWarning() << "Failed to remove project:" << q.lastError().text();
}

QVariantList SqliteMemory::listProjects() {
    QVariantList out;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, name, root_path FROM projects ORDER BY created_at ASC");
    if (q.exec()) {
        while (q.next()) {
            out.append(QVariantMap{
                {"id",       q.value(0).toString()},
                {"name",     q.value(1).toString()},
                {"rootPath", q.value(2).toString()},
            });
        }
    } else {
        qWarning() << "Failed to list projects:" << q.lastError().text();
    }
    return out;
}

// P2: 项目重命名（仅显示名，root_path 不变）。
void SqliteMemory::renameProject(const QString& projectId, const QString& name) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE projects SET name = ? WHERE id = ?");
    q.addBindValue(name);
    q.addBindValue(projectId);
    if (!q.exec())
        qWarning() << "Failed to rename project:" << q.lastError().text();
}

void SqliteMemory::setSessionProject(const QString& sessionId,
                                     const QString& projectId) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE sessions SET project_id = ? WHERE id = ?");
    // 空串绑定为 SQL NULL（脱离项目）。
    q.addBindValue(projectId.isEmpty() ? QVariant() : QVariant(projectId));
    q.addBindValue(sessionId);
    if (!q.exec())
        qWarning() << "Failed to set session project:" << q.lastError().text();
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

// ── docs/16 §4.2: 预置 11 个模型的公开定价 ───────────────
// INSERT OR IGNORE：首次启动写入，后续不覆盖用户自定义价格（用户可在
// 设置中改价，重启后保留）。本地 Ollama 模型定价 0，仍统计 token。
void SqliteMemory::seedModelPricing() {
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO model_pricing "
        "(model, input_price_cents, output_price_cents, cache_hit_price_cents, "
        "effective_date, notes) VALUES (?, ?, ?, ?, ?, ?)"));
    const qint64 now = QDateTime::currentDateTime().toMSecsSinceEpoch();
    // 价格单位：分/1M token（docs/16 §4.2 表格，2026 年公开价格）。
    const struct { const char* model; int inP, outP, chP; const char* notes; } presets[] = {
        {"gpt-4o",              250, 1000, 125, "OpenAI GPT-4o"},
        {"gpt-4o-mini",          15,   60,   8, "OpenAI GPT-4o-mini"},
        {"gpt-4-turbo",        1000, 3000, 500, "OpenAI GPT-4 Turbo"},
        {"o3-mini",             110,  440,  55, "OpenAI o3-mini"},
        {"claude-sonnet-4-5",   300, 1500,  30, "Anthropic Claude Sonnet 4.5"},
        {"claude-3-5-haiku",     80,  400,   8, "Anthropic Claude 3.5 Haiku"},
        {"claude-3-5-sonnet",   300, 1500,  30, "Anthropic Claude 3.5 Sonnet"},
        {"claude-3-opus",      1500, 7500, 150, "Anthropic Claude 3 Opus"},
        {"qwen2.5:7b",            0,    0,   0, "Ollama local model"},
        {"llama3.1:8b",           0,    0,   0, "Ollama local model"},
        {"deepseek-r1:7b",        0,    0,   0, "Ollama local model"},
    };
    for (const auto& p : presets) {
        query.addBindValue(QString::fromLatin1(p.model));
        query.addBindValue(p.inP);
        query.addBindValue(p.outP);
        query.addBindValue(p.chP);
        query.addBindValue(now);
        query.addBindValue(QString::fromLatin1(p.notes));
        if (!query.exec()) {
            qWarning() << "Failed to seed model_pricing for" << p.model
                       << ":" << query.lastError().text();
        }
    }
}

// ── docs/16 §4.4/§5.2: token_usage 明细写入 ─────────────
// record 字段名（驼峰）：requestId/sessionId/model/timestamp/inputTokens/
// outputTokens/cacheHitTokens/inputSystemPrompt.../costCents/latencyMs/
// workspaceId。缺失字段按 0/默认处理，timestamp 缺失用当前时间。
void SqliteMemory::logTokenUsage(const QVariantMap& record) {
    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO token_usage ("
        "request_id, session_id, model, timestamp, "
        "input_tokens, output_tokens, cache_hit_tokens, "
        "input_system_prompt, input_conversation, input_tool_call, "
        "input_mcp, input_skill, input_image, input_other, "
        "output_conversation, output_tool_call, output_mcp, "
        "output_skill, output_other, "
        "cache_hit_system_prompt, cache_hit_conversation, cache_hit_tool_call, "
        "cost_cents, latency_ms, workspace_id"
        ") VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    const auto getInt = [&](const char* key) {
        return record.value(QLatin1String(key)).toInt();
    };
    qint64 timestamp = record.value(QStringLiteral("timestamp")).toLongLong();
    if (timestamp <= 0) timestamp = QDateTime::currentDateTime().toMSecsSinceEpoch();
    QString ws = record.value(QStringLiteral("workspaceId")).toString();
    if (ws.isEmpty()) ws = QStringLiteral("local");

    query.addBindValue(static_cast<qint64>(record.value(QStringLiteral("requestId")).toLongLong()));
    query.addBindValue(record.value(QStringLiteral("sessionId")).toString());
    query.addBindValue(record.value(QStringLiteral("model")).toString());
    query.addBindValue(timestamp);
    query.addBindValue(getInt("inputTokens"));
    query.addBindValue(getInt("outputTokens"));
    query.addBindValue(getInt("cacheHitTokens"));
    query.addBindValue(getInt("inputSystemPrompt"));
    query.addBindValue(getInt("inputConversation"));
    query.addBindValue(getInt("inputToolCall"));
    query.addBindValue(getInt("inputMcp"));
    query.addBindValue(getInt("inputSkill"));
    query.addBindValue(getInt("inputImage"));
    query.addBindValue(getInt("inputOther"));
    query.addBindValue(getInt("outputConversation"));
    query.addBindValue(getInt("outputToolCall"));
    query.addBindValue(getInt("outputMcp"));
    query.addBindValue(getInt("outputSkill"));
    query.addBindValue(getInt("outputOther"));
    query.addBindValue(getInt("cacheHitSystemPrompt"));
    query.addBindValue(getInt("cacheHitConversation"));
    query.addBindValue(getInt("cacheHitToolCall"));
    query.addBindValue(static_cast<qint64>(record.value(QStringLiteral("costCents")).toLongLong()));
    query.addBindValue(static_cast<qint64>(record.value(QStringLiteral("latencyMs")).toLongLong()));
    query.addBindValue(ws);

    if (!query.exec()) {
        qWarning() << "Failed to log token usage:" << query.lastError().text();
    }
}

// ── docs/16 §9.2: 详细聚合统计（L3+L4+L6）─────────────
// 返回方向维度累计 + 来源维度累计 + 调用统计 + byModel + byDay。
QVariantMap SqliteMemory::tokenStatsDetailed(int recentDays) {
    QVariantMap out;
    if (recentDays <= 0) recentDays = 30;
    const qint64 cutoff = QDateTime::currentDateTime().addDays(-recentDays)
                              .toMSecsSinceEpoch();

    QSqlQuery total(m_db);
    total.prepare(QStringLiteral(
        "SELECT COUNT(*), "
        "COALESCE(SUM(input_tokens),0), COALESCE(SUM(output_tokens),0), "
        "COALESCE(SUM(cache_hit_tokens),0), "
        "COALESCE(SUM(input_system_prompt),0), COALESCE(SUM(input_conversation),0), "
        "COALESCE(SUM(input_tool_call),0), COALESCE(SUM(input_mcp),0), "
        "COALESCE(SUM(input_skill),0), COALESCE(SUM(input_image),0), "
        "COALESCE(SUM(input_other),0), "
        "COALESCE(SUM(output_conversation),0), COALESCE(SUM(output_tool_call),0), "
        "COALESCE(SUM(output_mcp),0), COALESCE(SUM(output_skill),0), "
        "COALESCE(SUM(output_other),0), "
        "COALESCE(SUM(cache_hit_system_prompt),0), COALESCE(SUM(cache_hit_conversation),0), "
        "COALESCE(SUM(cache_hit_tool_call),0), "
        "COALESCE(SUM(cost_cents),0), CAST(COALESCE(AVG(latency_ms),0) AS INTEGER) "
        "FROM token_usage WHERE timestamp >= ?"));
    total.addBindValue(cutoff);
    qint64 totalInput = 0, totalOutput = 0, totalCacheHit = 0, totalCost = 0;
    int totalCalls = 0, avgLatency = 0;
    if (total.exec() && total.next()) {
        totalCalls    = total.value(0).toInt();
        totalInput    = total.value(1).toLongLong();
        totalOutput   = total.value(2).toLongLong();
        totalCacheHit = total.value(3).toLongLong();
        out["totalInputSystemPrompt"]    = total.value(4).toLongLong();
        out["totalInputConversation"]    = total.value(5).toLongLong();
        out["totalInputToolCall"]        = total.value(6).toLongLong();
        out["totalInputMcp"]             = total.value(7).toLongLong();
        out["totalInputSkill"]           = total.value(8).toLongLong();
        out["totalInputImage"]           = total.value(9).toLongLong();
        out["totalInputOther"]           = total.value(10).toLongLong();
        out["totalOutputConversation"]   = total.value(11).toLongLong();
        out["totalOutputToolCall"]       = total.value(12).toLongLong();
        out["totalOutputMcp"]            = total.value(13).toLongLong();
        out["totalOutputSkill"]          = total.value(14).toLongLong();
        out["totalOutputOther"]          = total.value(15).toLongLong();
        out["totalCacheHitSystemPrompt"] = total.value(16).toLongLong();
        out["totalCacheHitConversation"] = total.value(17).toLongLong();
        out["totalCacheHitToolCall"]     = total.value(18).toLongLong();
        totalCost   = total.value(19).toLongLong();
        avgLatency = total.value(20).toInt();
    } else {
        qWarning() << "tokenStatsDetailed totals failed:" << total.lastError().text();
    }
    out["totalCalls"]        = totalCalls;
    out["totalInputTokens"]  = totalInput;
    out["totalOutputTokens"] = totalOutput;
    out["totalCacheHitTokens"] = totalCacheHit;
    out["totalTokens"]       = totalInput + totalOutput;
    out["totalCostCents"]    = totalCost;
    out["avgLatencyMs"]      = avgLatency;
    out["cacheHitRate"]      = totalInput > 0
        ? static_cast<double>(totalCacheHit) / static_cast<double>(totalInput)
        : 0.0;

    QVariantList byModel;
    QSqlQuery modelQ(m_db);
    modelQ.prepare(QStringLiteral(
        "SELECT model, COUNT(*), "
        "COALESCE(SUM(input_tokens),0), COALESCE(SUM(output_tokens),0), "
        "COALESCE(SUM(cache_hit_tokens),0), COALESCE(SUM(cost_cents),0), "
        "CAST(COALESCE(AVG(latency_ms),0) AS INTEGER) "
        "FROM token_usage WHERE timestamp >= ? GROUP BY model "
        "ORDER BY SUM(input_tokens)+SUM(output_tokens) DESC"));
    modelQ.addBindValue(cutoff);
    if (modelQ.exec()) {
        while (modelQ.next()) {
            byModel.append(QVariantMap{
                {QStringLiteral("model"),          modelQ.value(0).toString()},
                {QStringLiteral("calls"),          modelQ.value(1).toInt()},
                {QStringLiteral("inputTokens"),    modelQ.value(2).toLongLong()},
                {QStringLiteral("outputTokens"),   modelQ.value(3).toLongLong()},
                {QStringLiteral("cacheHitTokens"), modelQ.value(4).toLongLong()},
                {QStringLiteral("costCents"),      modelQ.value(5).toLongLong()},
                {QStringLiteral("avgLatencyMs"),   modelQ.value(6).toInt()},
            });
        }
    } else {
        qWarning() << "tokenStatsDetailed byModel failed:" << modelQ.lastError().text();
    }
    out["byModel"] = byModel;

    QVariantList byDay;
    QSqlQuery dayQ(m_db);
    dayQ.prepare(QStringLiteral(
        "SELECT date(timestamp/1000,'unixepoch','localtime') AS d, COUNT(*), "
        "COALESCE(SUM(input_tokens),0), COALESCE(SUM(output_tokens),0), "
        "COALESCE(SUM(cache_hit_tokens),0), COALESCE(SUM(cost_cents),0) "
        "FROM token_usage WHERE timestamp >= ? GROUP BY d ORDER BY d DESC LIMIT 31"));
    dayQ.addBindValue(cutoff);
    if (dayQ.exec()) {
        while (dayQ.next()) {
            byDay.append(QVariantMap{
                {QStringLiteral("day"),            dayQ.value(0).toString()},
                {QStringLiteral("calls"),          dayQ.value(1).toInt()},
                {QStringLiteral("inputTokens"),    dayQ.value(2).toLongLong()},
                {QStringLiteral("outputTokens"),   dayQ.value(3).toLongLong()},
                {QStringLiteral("cacheHitTokens"), dayQ.value(4).toLongLong()},
                {QStringLiteral("costCents"),      dayQ.value(5).toLongLong()},
            });
        }
    } else {
        qWarning() << "tokenStatsDetailed byDay failed:" << dayQ.lastError().text();
    }
    out["byDay"] = byDay;

    return out;
}

// ── docs/16 §9.2: 按来源聚合（L5）─────────────────────
// 7 个来源各返回 {source, inputTokens, outputTokens, cacheHitTokens, costCents}。
// costCents 按来源 token 占总 token 比例分摊（明细表只存整次请求总费用）。
QVariantList SqliteMemory::tokenStatsBySource(int recentDays) {
    QVariantList out;
    if (recentDays <= 0) recentDays = 30;
    const qint64 cutoff = QDateTime::currentDateTime().addDays(-recentDays)
                              .toMSecsSinceEpoch();

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT "
        "COALESCE(SUM(input_system_prompt),0), COALESCE(SUM(input_conversation),0), "
        "COALESCE(SUM(input_tool_call),0), COALESCE(SUM(input_mcp),0), "
        "COALESCE(SUM(input_skill),0), COALESCE(SUM(input_image),0), "
        "COALESCE(SUM(input_other),0), "
        "COALESCE(SUM(output_conversation),0), COALESCE(SUM(output_tool_call),0), "
        "COALESCE(SUM(output_mcp),0), COALESCE(SUM(output_skill),0), "
        "COALESCE(SUM(output_other),0), "
        "COALESCE(SUM(cache_hit_system_prompt),0), COALESCE(SUM(cache_hit_conversation),0), "
        "COALESCE(SUM(cache_hit_tool_call),0), "
        "COALESCE(SUM(cost_cents),0), "
        "COALESCE(SUM(input_tokens),0)+COALESCE(SUM(output_tokens),0) "
        "FROM token_usage WHERE timestamp >= ?"));
    q.addBindValue(cutoff);
    if (!q.exec() || !q.next()) {
        qWarning() << "tokenStatsBySource failed:" << q.lastError().text();
        return out;
    }
    const qint64 inSp     = q.value(0).toLongLong();
    const qint64 inConv   = q.value(1).toLongLong();
    const qint64 inTool   = q.value(2).toLongLong();
    const qint64 inMcp    = q.value(3).toLongLong();
    const qint64 inSkill  = q.value(4).toLongLong();
    const qint64 inImg    = q.value(5).toLongLong();
    const qint64 inOther  = q.value(6).toLongLong();
    const qint64 outConv  = q.value(7).toLongLong();
    const qint64 outTool  = q.value(8).toLongLong();
    const qint64 outMcp   = q.value(9).toLongLong();
    const qint64 outSkill = q.value(10).toLongLong();
    const qint64 outOther = q.value(11).toLongLong();
    const qint64 chSp     = q.value(12).toLongLong();
    const qint64 chConv   = q.value(13).toLongLong();
    const qint64 chTool   = q.value(14).toLongLong();
    const qint64 totalCost   = q.value(15).toLongLong();
    const qint64 totalTokens = q.value(16).toLongLong();

    // 费用按 token 占比分摊到各来源（整数除法，余数归入最大来源可接受）。
    const auto share = [totalTokens, totalCost](qint64 srcTokens) -> qint64 {
        if (totalTokens <= 0) return 0;
        return totalCost * srcTokens / totalTokens;
    };
    const struct { const char* name; qint64 inT, outT, chT; } rows[] = {
        {"system_prompt", inSp,    0,       chSp},
        {"conversation",  inConv,  outConv, chConv},
        {"tool_call",     inTool,  outTool, chTool},
        {"mcp",           inMcp,   outMcp,  0},
        {"skill",         inSkill, outSkill, 0},
        {"image",         inImg,   0,       0},
        {"other",         inOther, outOther, 0},
    };
    for (const auto& r : rows) {
        QVariantMap m;
        m[QStringLiteral("source")]         = QString::fromLatin1(r.name);
        m[QStringLiteral("inputTokens")]    = r.inT;
        m[QStringLiteral("outputTokens")]   = r.outT;
        m[QStringLiteral("cacheHitTokens")] = r.chT;
        m[QStringLiteral("costCents")]      = share(r.inT + r.outT + r.chT);
        out.append(m);
    }
    return out;
}

// ── docs/16 §8.3: 会话级 token 细节聚合（TopBar 悬停浮层）──
// 对单个会话的 token_usage 行做方向/来源拆分汇总。注意这是会话累计口径：
// 各次请求会重复计入重叠的历史上下文，输出/费用类指标只有累计有意义。
QVariantMap SqliteMemory::sessionTokenBreakdown(const QString& sessionId) {
    QVariantMap out;
    if (sessionId.isEmpty()) return out;

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT COUNT(*), "
        "COALESCE(SUM(input_tokens),0), COALESCE(SUM(output_tokens),0), "
        "COALESCE(SUM(cache_hit_tokens),0), "
        "COALESCE(SUM(input_system_prompt),0), COALESCE(SUM(input_conversation),0), "
        "COALESCE(SUM(input_tool_call),0), COALESCE(SUM(input_mcp),0), "
        "COALESCE(SUM(input_skill),0), COALESCE(SUM(input_image),0), "
        "COALESCE(SUM(input_other),0), "
        "COALESCE(SUM(output_conversation),0), COALESCE(SUM(output_tool_call),0), "
        "COALESCE(SUM(output_mcp),0), COALESCE(SUM(output_skill),0), "
        "COALESCE(SUM(output_other),0), "
        "COALESCE(SUM(cost_cents),0) "
        "FROM token_usage WHERE session_id = ?"));
    q.addBindValue(sessionId);
    if (!q.exec() || !q.next()) {
        qWarning() << "sessionTokenBreakdown failed:" << q.lastError().text();
        return out;
    }
    const qint64 inSp    = q.value(4).toLongLong();
    const qint64 inConv  = q.value(5).toLongLong();
    const qint64 inTool  = q.value(6).toLongLong();
    const qint64 inMcp   = q.value(7).toLongLong();
    const qint64 inSkill = q.value(8).toLongLong();
    const qint64 inImg   = q.value(9).toLongLong();
    const qint64 inOther = q.value(10).toLongLong();
    const qint64 totalInput  = q.value(1).toLongLong();
    const qint64 totalOutput = q.value(2).toLongLong();
    const qint64 totalCache  = q.value(3).toLongLong();

    out["totalCalls"]           = q.value(0).toInt();
    out["totalInputTokens"]     = totalInput;
    out["totalOutputTokens"]    = totalOutput;
    out["totalCacheHitTokens"]  = totalCache;
    out["totalTokens"]          = totalInput + totalOutput;
    out["totalCostCents"]       = q.value(16).toLongLong();
    out["inputSystemPrompt"]    = inSp;
    out["inputConversation"]    = inConv;
    out["inputToolCall"]        = inTool;
    out["inputMcp"]             = inMcp;
    out["inputSkill"]           = inSkill;
    out["inputImage"]           = inImg;
    out["inputOther"]           = inOther;
    out["outputConversation"]   = q.value(11).toLongLong();
    out["outputToolCall"]       = q.value(12).toLongLong();
    out["outputMcp"]            = q.value(13).toLongLong();
    out["outputSkill"]          = q.value(14).toLongLong();
    out["outputOther"]          = q.value(15).toLongLong();
    // 缓存命中率：缓存命中相对输入合计的占比。
    out["cacheHitRate"]         = totalInput > 0
        ? static_cast<double>(totalCache) / static_cast<double>(totalInput)
        : 0.0;
    return out;
}

// ── docs/16 §6.3: 成本核算聚合 ─────────────────────────
// totalCostCents 取已存储的 SUM(cost_cents)；input/output/cacheHit 费用
// 按各模型定价重新拆分（明细表只存总费用）。projectedMonthlyCents 按当月
// 日均推算。
QVariantMap SqliteMemory::costStats(int recentDays) {
    QVariantMap out;
    if (recentDays <= 0) recentDays = 30;
    const qint64 cutoff = QDateTime::currentDateTime().addDays(-recentDays)
                              .toMSecsSinceEpoch();

    QSqlQuery modelQ(m_db);
    modelQ.prepare(QStringLiteral(
        "SELECT model, COUNT(*), "
        "COALESCE(SUM(input_tokens),0), COALESCE(SUM(output_tokens),0), "
        "COALESCE(SUM(cache_hit_tokens),0), COALESCE(SUM(cost_cents),0) "
        "FROM token_usage WHERE timestamp >= ? GROUP BY model "
        "ORDER BY SUM(cost_cents) DESC"));
    modelQ.addBindValue(cutoff);

    qint64 totalCost = 0, inputCost = 0, outputCost = 0, cacheHitCost = 0;
    QVariantList byModel;
    if (modelQ.exec()) {
        while (modelQ.next()) {
            const QString model = modelQ.value(0).toString();
            const int calls     = modelQ.value(1).toInt();
            const qint64 inT    = modelQ.value(2).toLongLong();
            const qint64 outT   = modelQ.value(3).toLongLong();
            const qint64 chT    = modelQ.value(4).toLongLong();
            const qint64 storedCost = modelQ.value(5).toLongLong();
            // 按模型定价拆分各方向费用（分），与 calculateCost 同公式。
            int inP = 0, outP = 0, chP = 0;
            PricingCalculator::getPricing(model, inP, outP, chP);
            const qint64 billableInput = qMax(qint64(0), inT - chT);
            const qint64 mInput  = (billableInput * inP  + kPerMillion - 1) / kPerMillion;
            const qint64 mCache  = (qMax(qint64(0), chT) * chP + kPerMillion - 1) / kPerMillion;
            const qint64 mOutput = (outT * outP + kPerMillion - 1) / kPerMillion;
            inputCost    += mInput;
            cacheHitCost += mCache;
            outputCost   += mOutput;
            totalCost    += storedCost;
            byModel.append(QVariantMap{
                {QStringLiteral("model"),     model},
                {QStringLiteral("calls"),     calls},
                {QStringLiteral("tokens"),    inT + outT},
                {QStringLiteral("costCents"), storedCost},
            });
        }
    } else {
        qWarning() << "costStats byModel failed:" << modelQ.lastError().text();
    }
    out["totalCostCents"]    = totalCost;
    out["inputCostCents"]    = inputCost;
    out["outputCostCents"]   = outputCost;
    out["cacheHitCostCents"] = cacheHitCost;
    out["byModel"]           = byModel;

    QVariantList byDay;
    QSqlQuery dayQ(m_db);
    dayQ.prepare(QStringLiteral(
        "SELECT date(timestamp/1000,'unixepoch','localtime') AS d, COUNT(*), "
        "COALESCE(SUM(input_tokens),0)+COALESCE(SUM(output_tokens),0), "
        "COALESCE(SUM(cost_cents),0) "
        "FROM token_usage WHERE timestamp >= ? GROUP BY d ORDER BY d DESC LIMIT 31"));
    dayQ.addBindValue(cutoff);
    if (dayQ.exec()) {
        while (dayQ.next()) {
            byDay.append(QVariantMap{
                {QStringLiteral("day"),       dayQ.value(0).toString()},
                {QStringLiteral("calls"),     dayQ.value(1).toInt()},
                {QStringLiteral("tokens"),    dayQ.value(2).toLongLong()},
                {QStringLiteral("costCents"), dayQ.value(3).toLongLong()},
            });
        }
    } else {
        qWarning() << "costStats byDay failed:" << dayQ.lastError().text();
    }
    out["byDay"] = byDay;

    // 预估月费用：当月已消耗 / 已过天数 × 当月总天数。
    const QDate today      = QDate::currentDate();
    const QDate monthStart(today.year(), today.month(), 1);
    const int daysElapsed = monthStart.daysTo(today) + 1;
    qint64 monthlyUsed = 0;
    {
        const qint64 monthCutoff = QDateTime(monthStart, QTime(0, 0)).toMSecsSinceEpoch();
        QSqlQuery mq(m_db);
        mq.prepare(QStringLiteral(
            "SELECT COALESCE(SUM(cost_cents),0) FROM token_usage WHERE timestamp >= ?"));
        mq.addBindValue(monthCutoff);
        if (mq.exec() && mq.next()) monthlyUsed = mq.value(0).toLongLong();
    }
    const int daysInMonth = today.daysInMonth();
    out["projectedMonthlyCents"] = daysElapsed > 0
        ? monthlyUsed * daysInMonth / daysElapsed
        : 0;

    return out;
}

// ── docs/16 §7.2: 配额状态查询 ─────────────────────────
// 返回当日/当月已消耗 token 与费用（分），供 AIAgent::checkQuota 判断。
QVariantMap SqliteMemory::quotaStatus() {
    QVariantMap out;
    const QDate today      = QDate::currentDate();
    const QDate monthStart(today.year(), today.month(), 1);
    const qint64 dayCutoff   = QDateTime(today,      QTime(0, 0)).toMSecsSinceEpoch();
    const qint64 monthCutoff = QDateTime(monthStart, QTime(0, 0)).toMSecsSinceEpoch();

    QSqlQuery q(m_db);
    // 当日
    q.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(input_tokens),0)+COALESCE(SUM(output_tokens),0), "
        "COALESCE(SUM(cost_cents),0) FROM token_usage WHERE timestamp >= ?"));
    q.addBindValue(dayCutoff);
    qint64 dailyTokens = 0, dailyCost = 0;
    if (q.exec() && q.next()) {
        dailyTokens = q.value(0).toLongLong();
        dailyCost   = q.value(1).toLongLong();
    } else {
        qWarning() << "quotaStatus daily failed:" << q.lastError().text();
    }
    // 当月
    q.prepare(QStringLiteral(
        "SELECT COALESCE(SUM(input_tokens),0)+COALESCE(SUM(output_tokens),0), "
        "COALESCE(SUM(cost_cents),0) FROM token_usage WHERE timestamp >= ?"));
    q.addBindValue(monthCutoff);
    qint64 monthlyTokens = 0, monthlyCost = 0;
    if (q.exec() && q.next()) {
        monthlyTokens = q.value(0).toLongLong();
        monthlyCost   = q.value(1).toLongLong();
    } else {
        qWarning() << "quotaStatus monthly failed:" << q.lastError().text();
    }

    out["dailyTokensUsed"]      = dailyTokens;
    out["dailyCostCentsUsed"]   = dailyCost;
    out["monthlyTokensUsed"]    = monthlyTokens;
    out["monthlyCostCentsUsed"] = monthlyCost;
    return out;
}

} // namespace NexusAI
