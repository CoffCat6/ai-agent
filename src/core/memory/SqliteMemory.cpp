#include "SqliteMemory.h"

#include <QHash>
#include <QSqlQuery>
#include <QSqlError>
#include <QRegularExpression>
#include <QVariant>
#include <QVector>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
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

// docs/25 B2: same row-ceiling discipline for the episodic LIKE fallback.
// The store holds one summary per session, so the ceiling is proportionally
// smaller than the messages one.
constexpr qint64 kLikeScanMaxEpisodic = 10000;

// docs/25 B2: candidate ceiling for the episodic semantic scan. The scan is
// a bounded in-memory cosine rerank (BLOB decode + dot product per row), so
// the cap keeps memory and CPU bounded on long-lived stores. Episodes older
// than the cap are unreachable by semantic retrieval — a full candidate set
// is logged loudly below so the truncation is discoverable, not silent.
constexpr int kSemanticCandidateCap = 500;

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

    // docs/24 R1: connection setup extracted so the restore path can close
    // and re-open in place (restoreFromBackup) with identical semantics.
    openConnection();
}

bool SqliteMemory::openConnection() {
    // Unique per-instance connection name; stored in m_connName so the
    // destructor can removeDatabase it (MEM-702) - an unstored name leaked
    // the connection for the lifetime of the process.
    m_connName = QStringLiteral("nexus_memory_%1")
                     .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connName);
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        qWarning() << "Failed to open memory database:" << m_db.lastError().text();
        return false;
    }

    // Batch 29: parallel session tabs each own a connection to the same
    // database file; the wait bounds concurrent-write failures with
    // SQLITE_BUSY instead of failing the write outright.
    QSqlQuery busy(m_db);
    busy.exec("PRAGMA busy_timeout = 5000");

    // docs/23 G2: WAL — readers no longer block on a concurrent writer
    // (per-tab GUI threads read while another tab streams writes), and
    // synchronous=NORMAL (the SQLite-recommended WAL pairing) defers
    // fsync to checkpoint time; a power cut may lose the last committed
    // transaction but never corrupts the database. Idempotent per
    // connection: journal_mode is a persistent property of the file.
    // Note: WAL sidecar files make raw .db copies invalid — database
    // backups must go through VACUUM INTO (docs/23 D1).
    QSqlQuery wal(m_db);
    wal.exec("PRAGMA journal_mode = WAL");
    wal.exec("PRAGMA synchronous = NORMAL");

    initDatabase();
    return true;
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
    // docs/23 G1: schema 版本标记（lite）。0 = 老库升级或新库首建：本轮
    // 执行完整迁移 + FTS 回填，末尾 ANALYZE 后落 user_version=1；此后
    // 启动跳过回填与 ANALYZE（触发器已保证 FTS 同步，稳态回填是对
    // messages 的 O(n) 纯扫描）。
    QSqlQuery versionQuery(m_db);
    int userVersion = 0;
    if (versionQuery.exec("PRAGMA user_version") && versionQuery.next())
        userVersion = versionQuery.value(0).toInt();
    // The PRAGMA above returns a row, so its statement stays active on the
    // connection until the query object is reused or destroyed. Finalize it
    // before the migrations: an active statement holds a read lock on the
    // schema, and the virtual-table DROP in migrateEpisodicFts() then fails
    // with SQLITE_LOCKED ("database table is locked").
    versionQuery.finish();

    createTables();
    // docs/17 A0: sessions.project_id 列补齐（projects 表在 createTables 里）。
    migrateSessionProjectColumn();
    migrateTraceNodeReasoningColumn();
    migrateConversationModel(userVersion);
    migrateKnowledgeSchema();
    // docs/25: 引用定位——chunk 页码列（PDF 导入写入，-1 = 无页码）。
    migrateKnowledgeChunkPageColumn();
    migrateMessagesFts(/*backfillRows=*/userVersion == 0);
    migrateEpisodicFts();
    // docs/16 §4.2/§6.2: 预置公开定价（不覆盖用户自定义），并载入
    // PricingCalculator 进程级缓存，供 costStats 等聚合查询使用。
    seedModelPricing();
    PricingCalculator::refreshCache(m_db);

    // docs/23 G1: 两个最高频查询路径的索引。messages 是唯一随使用量
    // 无界增长且按 session_id 过滤的主表（loadMessages/deleteMessagesFrom/
    // lastUserMessageId 等），sessions 的侧栏排序按 pinned/updated_at。
    // 索引条目隐式以 rowid 结尾，(session_id, timestamp) 直接支撑
    // ORDER BY timestamp DESC, rowid DESC 的反序扫描。
    // sessions.pinned 由上面的补列迁移引入，索引必须在其之后创建。
    execDdl(versionQuery,
            "CREATE INDEX IF NOT EXISTS idx_messages_session_ts "
            "ON messages(session_id, timestamp)",
            "create idx_messages_session_ts");
    execDdl(versionQuery,
            "CREATE INDEX IF NOT EXISTS idx_sessions_pinned_updated "
            "ON sessions(pinned, updated_at)",
            "create idx_sessions_pinned_updated");
    execDdl(versionQuery, "DROP INDEX IF EXISTS idx_messages_session_parent",
            "drop the retired message-tree index");

    if (userVersion == 0) {
        execDdl(versionQuery, "ANALYZE", "analyze for the query planner");
    }
    if (userVersion < 3)
        execDdl(versionQuery, "PRAGMA user_version = 3", "stamp user_version");
}

// Batch 17 (FEAT-1701): messages FTS5 index for cross-session search.
// Tokenizer choice from the recorded probe (SQLite 3.49.1, this repo's
// bundled Qt): the default unicode61 tokenizer cannot substring-match CJK
// text (a whole sentence becomes one token), while trigram answers
// 3+ character CJK/ASCII substring queries. Keywords shorter than 3 chars
// fall back to LIKE in searchMessages(). Idempotent self-heal (discipline
// #8): everything is IF NOT EXISTS; the one-shot backfill covers rows
// written before this migration.
// docs/23 G1: backfillRows gates the O(messages) NOT-IN backfill to the
// upgrade/first-create path (user_version==0) — on a stamped database the
// triggers already keep the index in sync, so the steady-state scan is
// pure waste on every startup.
void SqliteMemory::migrateMessagesFts(bool backfillRows) {
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
    if (!backfillRows) return;
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
    // Probe the current schema in its own scope: the SELECT stays active on
    // the connection until finalized, and an active schema-reading statement
    // makes the DROP/CREATE VIRTUAL TABLE below fail with SQLITE_LOCKED
    // ("database table is locked").
    QString sql;
    {
        QSqlQuery check(m_db);
        check.prepare("SELECT sql FROM sqlite_master "
                      "WHERE type='table' AND name='episodic_fts'");
        if (!check.exec() || !check.next()) return; // absent — createTables owns it
        sql = check.value(0).toString();
    }
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
    // Batch 7 MEM-704 - always 'local' until multi-tenant support).
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

    // docs/25 B1: episodic indexes. The semantic scan orders by timestamp
    // DESC and clear()/hardDeleteSessionRows() filter by session_id — both
    // used to degrade into full scans as the store grows. IF NOT EXISTS
    // (same discipline as idx_messages_session_ts) so existing databases
    // upgrade in place on the next open.
    execDdl(query, "CREATE INDEX IF NOT EXISTS idx_episodic_ts ON episodic(timestamp)",
            "create idx_episodic_ts");
    execDdl(query, "CREATE INDEX IF NOT EXISTS idx_episodic_session ON episodic(session_id)",
            "create idx_episodic_session");

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
            reasoning TEXT NOT NULL DEFAULT '',
            timestamp INTEGER NOT NULL,
            message_id INTEGER NOT NULL DEFAULT 0,
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
    QStringList columns;
    if (!q.exec("PRAGMA table_info(sessions)")) return;
    while (q.next()) columns.append(q.value(1).toString());
    if (!columns.contains("pinned"))
        execDdl(q, "ALTER TABLE sessions ADD COLUMN pinned INTEGER NOT NULL DEFAULT 0",
                "add sessions.pinned column");
    if (!columns.contains("archived"))
        execDdl(q, "ALTER TABLE sessions ADD COLUMN archived INTEGER NOT NULL DEFAULT 0",
                "add sessions.archived column");
    // docs/23 G7: 软删除标记（NULL/0 = 未删除；删除时刻为毫秒时间戳）。
    if (!columns.contains("deleted_at"))
        execDdl(q, "ALTER TABLE sessions ADD COLUMN deleted_at INTEGER",
                "add sessions.deleted_at column");
}

// 思考正文按列存（trace_nodes 是列映射表，不是整节点 JSON 大字段）：老库
// 缺列就补，新库由建表 DDL 带上。缺了这一步，重启恢复会话时思考正文会静默
// 消失——save/load 绑不上也查不到该列。
void SqliteMemory::migrateTraceNodeReasoningColumn() {
    QSqlQuery pragma(m_db);
    QStringList columns;
    if (pragma.exec("PRAGMA table_info(trace_nodes)")) {
        while (pragma.next())
            columns << pragma.value(1).toString();
    }
    if (columns.isEmpty()) return;   // 表还没建（新库由 DDL 负责）
    if (!columns.contains(QLatin1String("reasoning")))
        execDdl(pragma, "ALTER TABLE trace_nodes ADD COLUMN reasoning "
                        "TEXT NOT NULL DEFAULT ''",
                "add trace_nodes.reasoning column");
    if (!columns.contains(QLatin1String("metadata")))
        execDdl(pragma, "ALTER TABLE trace_nodes ADD COLUMN metadata TEXT NOT NULL DEFAULT '{}'",
                "add trace_nodes.metadata column");
}

// 把中间那版消息树拉平回线性历史，一次性（user_version < 3），一个事务：按 head
// 的祖先链定出用户当时在看的那条线，链外的重复答案行连同其轨迹节点删掉；清空
// parent_id / head_message_id（列留着——真实库上重建三张主表的风险大于多两个空列）；
// DROP 遗留的 message_variants。没经历过树版本的库整段退化为 no-op。
void SqliteMemory::migrateConversationModel(int userVersion) {
    if (userVersion >= 3) return;
    if (!m_db.transaction()) return;

    const auto hasColumn = [this](const char* table, const char* column) {
        QSqlQuery pragma(m_db);
        if (!pragma.exec(QString("PRAGMA table_info(%1)").arg(QLatin1String(table))))
            return false;
        bool found = false;
        while (pragma.next()) {
            if (pragma.value(1).toString() == QLatin1String(column)) { found = true; break; }
        }
        pragma.finish();       // 语句握着读锁时后面的 DDL/DML 会失败
        return found;
    };
    const auto abort = [this](const char* where, const QSqlQuery& query) {
        qWarning() << "Conversation-model migration aborted at" << where << ":"
                   << query.lastError().text();
        m_db.rollback();
    };

    const bool hasTree = hasColumn("messages", "parent_id")
                      && hasColumn("sessions", "head_message_id");
    if (hasTree) {
        QList<QPair<QString, qint64>> heads;
        {
            QSqlQuery list(m_db);
            if (!list.exec("SELECT id, head_message_id FROM sessions WHERE head_message_id > 0")) {
                abort("session heads", list);
                return;
            }
            while (list.next())
                heads.append({list.value(0).toString(), list.value(1).toLongLong()});
            list.finish();
        }
        for (const auto& head : std::as_const(heads)) {
            const QString sessionId = head.first;
            QList<qint64> onPath;
            {
                QSqlQuery walk(m_db);
                walk.prepare("WITH RECURSIVE anc(id, p) AS ("
                             "  SELECT id, parent_id FROM messages WHERE session_id = ? AND id = ?"
                             "  UNION ALL"
                             "  SELECT m.id, m.parent_id FROM messages m JOIN anc a ON m.id = a.p"
                             "  WHERE m.session_id = ? AND a.id <> m.id"
                             ") SELECT id FROM anc");
                walk.addBindValue(sessionId);
                walk.addBindValue(head.second);
                walk.addBindValue(sessionId);
                if (!walk.exec()) { abort("ancestor walk", walk); return; }
                while (walk.next()) onPath << walk.value(0).toLongLong();
                walk.finish();
            }
            if (onPath.isEmpty()) continue;   // head 指的行已不在：只清指针

            QList<qint64> abandoned;
            {
                QSqlQuery rows(m_db);
                rows.prepare("SELECT id FROM messages WHERE session_id = ? ORDER BY id ASC");
                rows.addBindValue(sessionId);
                if (!rows.exec()) { abort("row scan", rows); return; }
                while (rows.next()) {
                    const qint64 id = rows.value(0).toLongLong();
                    if (!onPath.contains(id)) abandoned << id;
                }
                rows.finish();
            }
            if (!abandoned.isEmpty()) {
                const QString placeholders = QStringList(abandoned.count(), QStringLiteral("?"))
                                                 .join(QStringLiteral(","));
                {
                    QSqlQuery nodes(m_db);
                    nodes.prepare(QString("DELETE FROM trace_nodes WHERE session_id = ? "
                                          "AND message_id IN (") + placeholders + ")");
                    nodes.addBindValue(sessionId);
                    for (qint64 id : std::as_const(abandoned)) nodes.addBindValue(id);
                    if (!nodes.exec()) { abort("drop abandoned trace", nodes); return; }
                }
                {
                    QSqlQuery rows(m_db);
                    rows.prepare(QString("DELETE FROM messages WHERE session_id = ? AND id IN (")
                                 + placeholders + ")");
                    rows.addBindValue(sessionId);
                    for (qint64 id : std::as_const(abandoned)) rows.addBindValue(id);
                    if (!rows.exec()) { abort("drop abandoned rows", rows); return; }
                }
            }
        }
        {
            QSqlQuery clearParents(m_db);
            if (!clearParents.exec("UPDATE messages SET parent_id = NULL WHERE parent_id IS NOT NULL")) {
                abort("clear parent_id", clearParents);
                return;
            }
        }
        {
            QSqlQuery clearHead(m_db);
            if (!clearHead.exec("UPDATE sessions SET head_message_id = 0 WHERE head_message_id > 0")) {
                abort("clear head_message_id", clearHead);
                return;
            }
        }
    }

    QSqlQuery dropVariants(m_db);
    if (!dropVariants.exec("DROP TABLE IF EXISTS message_variants"))
        qWarning() << "Could not drop message_variants:" << dropVariants.lastError().text();

    if (!m_db.commit()) {
        qWarning() << "Conversation-model migration commit failed:"
                   << m_db.lastError().text();
        m_db.rollback();
    }
}

// docs/25: knowledge_chunks.page 列补齐（引用定位——chunk 页码；幂等）。
// 新库由建表 DDL 直接带上该列，本迁移只服务老库升级。
void SqliteMemory::migrateKnowledgeChunkPageColumn() {
    QSqlQuery q(m_db);
    if (!q.exec("PRAGMA table_info(knowledge_chunks)")) {
        qWarning() << "PRAGMA table_info(knowledge_chunks) failed:"
                   << q.lastError().text();
        return;
    }
    bool hasColumn = false;
    while (q.next()) {
        if (q.value(1).toString() == QLatin1String("page")) {
            hasColumn = true;
            break;
        }
    }
    if (!hasColumn)
        execDdl(q, "ALTER TABLE knowledge_chunks ADD COLUMN page INTEGER NOT NULL "
                   "DEFAULT -1",
                "add knowledge_chunks.page column");
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

    // MEM-704: workspace_id reservation on the session-scoped tables
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
            page INTEGER NOT NULL DEFAULT -1,
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
                  "text, embedding, dim, pseudo, page) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    for (const KnowledgeChunk& chunk : chunks) {
        query.addBindValue(name);
        query.addBindValue(chunk.index);
        query.addBindValue(chunk.total);
        query.addBindValue(chunk.text);
        query.addBindValue(embeddingToBlob(chunk.embedding));
        query.addBindValue(chunk.dim);
        query.addBindValue(chunk.pseudo ? 1 : 0);
        query.addBindValue(chunk.page);
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
    query.prepare("SELECT chunk_index, chunk_total, text, embedding, dim, pseudo, page "
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
            const QVariant pageValue = query.value(6);
            chunk.page = pageValue.isNull() ? -1 : pageValue.toInt();
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

namespace {

QJsonObject messageRowToObject(const QString& role, const QString& content,
                               const QString& metadataJson, double timestamp, qint64 id) {
    QJsonObject msg;
    msg["role"] = role;
    msg["content"] = content;
    msg["metadata"] = QJsonDocument::fromJson(metadataJson.toUtf8()).object();
    const QJsonObject metadata = msg.value("metadata").toObject();
    if (metadata.contains("images"))
        msg["images"] = metadata.value("images");
    if (metadata.contains("files"))
        msg["files"] = metadata.value("files");
    // Batch 18: msec epoch for export timestamps (consumers that don't need it
    // simply ignore the extra key).
    msg["timestamp"] = timestamp;
    msg["id"] = id;
    return msg;
}

} // namespace

qint64 SqliteMemory::saveMessage(const QString& sessionId, const QJsonObject& msg) {
    // 空会话 id 写进去就是一行无主消息，永远不会被 loadMessages 取到。
    if (sessionId.isEmpty()) return 0;
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
        return 0;
    }
    return query.lastInsertId().toLongLong();
}

QJsonArray SqliteMemory::loadMessages(const QString& sessionId, int limit) {
    QSqlQuery query(m_db);
    // Latest `limit` messages in chronological order: the inner DESC picks
    // the newest window (rowid breaks timestamp ties), the outer ASC
    // restores conversation order. A plain "ASC LIMIT ?" kept the OLDEST
    // messages and silently dropped the recent ones in long sessions.
    query.prepare("SELECT role, content, metadata, timestamp, rowid FROM ("
                  "SELECT role, content, metadata, timestamp, rowid FROM messages "
                  "WHERE session_id = ? "
                  "ORDER BY timestamp DESC, rowid DESC LIMIT ?"
                  ") ORDER BY timestamp ASC, rowid ASC");
    query.addBindValue(sessionId);
    query.addBindValue(limit);

    QJsonArray messages;
    if (query.exec()) {
        while (query.next())
            messages.append(messageRowToObject(query.value(0).toString(), query.value(1).toString(),
                                               query.value(2).toString(), query.value(3).toDouble(),
                                               query.value(4).toLongLong()));
    } else {
        // docs/23 G5: the only query in the file that failed silently —
        // a user-visible "history disappeared" with nothing in the log.
        qWarning() << "Failed to load messages:" << query.lastError().text();
    }
    return messages;
}

int SqliteMemory::deleteMessagesFrom(const QString& sessionId, qint64 fromId) {
    if (sessionId.isEmpty() || fromId <= 0) return 0;
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM messages WHERE session_id = ? AND id >= ?");
    query.addBindValue(sessionId);
    query.addBindValue(fromId);
    if (!query.exec()) {
        qWarning() << "Failed to delete messages:" << query.lastError().text();
        return 0;
    }
    return query.numRowsAffected();
}

// docs/20 §3.1（FORK-701）：会话内最后一条 user 消息的主键。AUTOINCREMENT
// id 单调，MAX(id) 即最后一条；限定 role='user' 过滤掉尾部 assistant 行。
qint64 SqliteMemory::lastUserMessageId(const QString& sessionId) {
    if (sessionId.isEmpty()) return 0;
    QSqlQuery query(m_db);
    query.prepare("SELECT MAX(id) FROM messages WHERE session_id = ? AND role = 'user'");
    query.addBindValue(sessionId);
    if (!query.exec()) {
        qWarning() << "Failed to find last user message:" << query.lastError().text();
        return 0;
    }
    return query.next() ? query.value(0).toLongLong() : 0;
}

bool SqliteMemory::truncateLastTurn(const QString& sessionId, bool removeUser) {
    if (sessionId.isEmpty() || !m_db.transaction()) return false;
    QSqlQuery query(m_db);
    const qint64 userId = lastUserMessageId(sessionId);
    bool ok = userId > 0;
    if (ok) {
        query.prepare("DELETE FROM trace_nodes WHERE session_id = ? AND node_order >= "
                      "(SELECT MAX(node_order) + ? FROM trace_nodes "
                      "WHERE session_id = ? AND type = 'user_message')");
        query.addBindValue(sessionId);
        query.addBindValue(removeUser ? 0 : 1);
        query.addBindValue(sessionId);
        ok = query.exec();
    }
    if (ok) {
        query.prepare("DELETE FROM messages WHERE session_id = ? AND id >= ?");
        query.addBindValue(sessionId);
        query.addBindValue(userId + (removeUser ? 0 : 1));
        ok = query.exec();
    }
    if (ok && m_db.commit()) return true;
    qWarning() << "Failed to truncate conversation turn:" << query.lastError().text();
    m_db.rollback();
    return false;
}

// 按序号切、不读 message_id：树迁移时代把整轮节点批量挂到当时"当前"的消息上，
// 那些脏值会把副本切得比消息列表长。
QString SqliteMemory::forkSessionAtAnswer(const QString& sourceSessionId, int answerRank,
                                          const QString& title) {
    if (sourceSessionId.isEmpty() || answerRank <= 0) return QString();

    qint64 anchorId = 0;
    {
        QSqlQuery anchor(m_db);
        anchor.prepare("SELECT id FROM messages WHERE session_id = ? AND role = 'assistant'"
                       " ORDER BY id ASC LIMIT 1 OFFSET ?");
        anchor.addBindValue(sourceSessionId);
        anchor.addBindValue(answerRank - 1);
        if (!anchor.exec() || !anchor.next()) {
            qWarning() << "Refused to fork" << sourceSessionId
                       << "at answer #" << answerRank << ":"
                       << (anchor.lastError().isValid()
                               ? anchor.lastError().text()
                               : QStringLiteral("no such answer in this session"));
            return QString();
        }
        anchorId = anchor.value(0).toLongLong();
    }

    // 切在那条回答所在轮次的末尾：它之后第一条提问之前的最后一个节点，没有下一条
    // 提问就切到时间线末尾。数不到 summary 时交给 resumeSession 用消息文本重建。
    int cutoff = -1;
    {
        QSqlQuery cut(m_db);
        cut.prepare("SELECT COALESCE("
                    "(SELECT MIN(t2.node_order) - 1 FROM trace_nodes t2"
                    "  WHERE t2.session_id = t.session_id AND t2.type = 'user_message'"
                    "  AND t2.node_order > t.node_order),"
                    "(SELECT MAX(t3.node_order) FROM trace_nodes t3"
                    "  WHERE t3.session_id = t.session_id),"
                    "t.node_order) "
                    "FROM trace_nodes t WHERE t.session_id = ? AND t.type = 'summary'"
                    " ORDER BY t.node_order ASC LIMIT 1 OFFSET ?");
        cut.addBindValue(sourceSessionId);
        cut.addBindValue(answerRank - 1);
        if (cut.exec() && cut.next() && !cut.value(0).isNull())
            cutoff = cut.value(0).toInt();
    }

    const QString newId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    beginSession(newId, title);           // 单条 INSERT，自身就是原子的

    bool ok = m_db.transaction();
    if (ok) {
        QSqlQuery copy(m_db);
        copy.prepare("INSERT INTO messages (session_id, role, content, timestamp, metadata, "
                     "workspace_id) SELECT ?, role, content, timestamp, metadata, workspace_id "
                     "FROM messages WHERE session_id = ? AND id <= ? ORDER BY id ASC");
        copy.addBindValue(newId);
        copy.addBindValue(sourceSessionId);
        copy.addBindValue(anchorId);
        ok = copy.exec();
        if (!ok) qWarning() << "Fork: copy messages failed:" << copy.lastError().text();
    }
    if (ok && cutoff >= 0) {
        QSqlQuery copyNodes(m_db);
        copyNodes.prepare("INSERT INTO trace_nodes (session_id, node_id, node_order, "
                          "type, title, status, args, result, reasoning, timestamp, "
                          "message_id, metadata, workspace_id) "
                          "SELECT ?, node_id, node_order, type, title, status, args, "
                          "result, reasoning, timestamp, 0, metadata, workspace_id "
                          "FROM trace_nodes WHERE session_id = ? AND node_order <= ?");
        copyNodes.addBindValue(newId);
        copyNodes.addBindValue(sourceSessionId);
        copyNodes.addBindValue(cutoff);
        ok = copyNodes.exec();
        if (!ok) qWarning() << "Fork: copy trace nodes failed:" << copyNodes.lastError().text();
    }
    if (ok) {
        // 分叉出去的对话留在源会话的项目归属里（项目聊天分叉后仍属于该项目）。
        QSqlQuery inherit(m_db);
        inherit.prepare("UPDATE sessions SET project_id = ("
                        "SELECT project_id FROM sessions WHERE id = ?) WHERE id = ?");
        inherit.addBindValue(sourceSessionId);
        inherit.addBindValue(newId);
        ok = inherit.exec();
        if (!ok) qWarning() << "Fork: inherit project failed:" << inherit.lastError().text();
    }
    if (ok && m_db.commit()) return newId;

    qWarning() << "Fork aborted; removing the half-copied session" << newId;
    m_db.rollback();
    hardDeleteSessionRows(newId);       // 半个会话留在列表里比失败更糟
    return QString();
}

// Batch 22 (search-aggregate): strip the markdown decorations a raw
// message body carries so the sidebar snippet shows plain text. Line-level
// constructs (fenced code blocks, heading markers, list bullets) are
// handled while the line structure still exists; inline markup is removed
// afterwards, longest marker first - "**bold**" must fall to the bold rule
// before the italic rule can match "*bold*" inside it, and images before
// links (a link match inside "![alt](url)" would leave a stray "!").
// Review fix (M-1): underscore-flavored bold/italic (__b__/_i_) are NOT
// stripped. No word boundary can separate them from code identifiers -
// "__init__" would lose its dunder and "user_name" would collapse to
// "username", while in a coding-assistant chat the identifier reading is
// far more likely than the emphasis one. Star-flavored markers already
// cover the bold/italic markup AI replies actually use; a residual "__"
// is harmless. Best-effort by design: it removes decoration only, never
// content, and unknown markup passes through untouched.
QString SqliteMemory::stripMarkdownDecorations(const QString& text) {
    static const QRegularExpression fence(QStringLiteral("^\\s*```"));
    static const QRegularExpression heading(QStringLiteral("^\\s{0,3}#{1,6}\\s+"));
    static const QRegularExpression bullet(QStringLiteral("^\\s*[-*+]\\s+"));
    static const QRegularExpression image(QStringLiteral("!\\[([^\\]]*)\\]\\([^)]*\\)"));
    static const QRegularExpression link(QStringLiteral("\\[([^\\]]*)\\]\\([^)]*\\)"));
    static const QRegularExpression codeSpan(QStringLiteral("`([^`]*)`"));
    static const QRegularExpression boldStar(QStringLiteral("\\*\\*(.+?)\\*\\*"));
    static const QRegularExpression italicStar(QStringLiteral("\\*(.+?)\\*"));
    // Review fix (Suggestion②): the newline splitter is now a static — the
    // per-call temporary QRegularExpression used to recompile the pattern on
    // every snippet, a hot path when many sessions are aggregated.
    static const QRegularExpression newline(QStringLiteral("\\r\\n|\\r|\\n"));

    // Fenced blocks: the ``` marker lines are dropped, the enclosed lines
    // are kept verbatim (no further stripping inside code).
    QStringList kept;
    bool inFence = false;
    const QStringList lines = text.split(newline);
    kept.reserve(lines.size());
    for (QString line : lines) {
        if (fence.match(line).hasMatch()) {
            inFence = !inFence;
            continue;
        }
        if (!inFence) {
            line.remove(heading);
            line.remove(bullet);
        }
        kept.append(line);
    }

    QString stripped = kept.join(QLatin1Char('\n'));
    // Inline markup, in removal order: images → links (image first, see
    // above), then code spans, then bold (long) before italic (short).
    // Underscore flavors deliberately absent - see the header comment.
    stripped.replace(image, QStringLiteral("\\1"));
    stripped.replace(link, QStringLiteral("\\1"));
    stripped.replace(codeSpan, QStringLiteral("\\1"));
    stripped.replace(boldStar, QStringLiteral("\\1"));
    stripped.replace(italicStar, QStringLiteral("\\1"));
    return stripped;
}

// Batch 22 (search-aggregate): turn a raw message body into the
// single-line sidebar snippet. Pipeline (the order is load-bearing):
//   1. strip markdown decorations (stripMarkdownDecorations);
//   2. flatten newlines and collapse runs of whitespace - the sidebar row
//      is one fixed-height line, embedded '\n' used to overflow it;
//   3. cut a bounded window centered on the first (case-insensitive) hit;
//   4. wrap the keyword in U+0001/U+0002 control-character highlight
//      markers - the contract the QML delegate renders. Review fix (m-1):
//      plain "[kw]" brackets collide with bracketed text messages carry
//      verbatim ("arr[0]", "[TODO]"); control characters cannot occur in
//      normal text, so the UI can tell injected markers from content.
//      Injection happens LAST: stripping markdown first is what keeps the
//      link rule from eating the marked keyword as link text.
// When the keyword cannot be found in the stripped text (e.g. it only
// existed inside stripped markup), a head window is shown without markers.
QString SqliteMemory::buildSearchSnippet(const QString& raw, const QString& keyword) {
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    QString flat = stripMarkdownDecorations(raw);
    flat.replace(whitespace, QStringLiteral(" "));
    flat = flat.trimmed();

    // ~48 context chars ≈ the window the previous FTS
    // snippet(..., 16 tokens) call produced.
    constexpr int kWindow = 48;
    int hit = flat.indexOf(keyword, 0, Qt::CaseInsensitive);
    QString snippet = flat;
    if (hit < 0) {
        if (flat.length() > kWindow)
            snippet = flat.left(kWindow) + QStringLiteral("…");
    } else {
        const int start = qMax(0, hit - kWindow / 2);
        const int end = qMin(flat.length(), hit + keyword.length() + kWindow / 2);
        snippet = (start > 0 ? QStringLiteral("…") : QString())
                  + flat.mid(start, end - start)
                  + (end < flat.length() ? QStringLiteral("…") : QString());
        // Re-locate inside the (possibly prefixed/trimmed) window.
        hit = snippet.indexOf(keyword, 0, Qt::CaseInsensitive);
    }

    if (hit >= 0 && !keyword.isEmpty()) {
        // U+0001/U+0002 delimit the injected marker - see header comment.
        snippet = snippet.left(hit) + QChar(u'\u0001')
                  + snippet.mid(hit, keyword.length()) + QChar(u'\u0002')
                  + snippet.mid(hit + keyword.length());
    }
    return snippet;
}

// Batch 17 (FEAT-1702): cross-session keyword search. Batch 22
// (search-aggregate): results are aggregated PER SESSION - one row per
// session {sessionId, sessionTitle, hitCount, latestTimestamp, archived,
// bestSnippet, bestRole, firstHitMessageId}, the session's newest hit
// first; `limit` caps the number of sessions. The trigram FTS path handles
// 3+ character keywords (quoted phrase → substring match, CJK included);
// shorter keywords fall back to a LIKE scan because a trigram index cannot
// answer sub-trigram queries.
//
// firstHitMessageId (A1 hit-locate) is the messages.id of the session's
// newest matching message — the jump target for the sidebar's
// locate-and-highlight flow. archived mirrors the session's soft-archive
// flag (Suggestion④) so the result card can render the same "已归档" badge
// the session list shows.
//
// Batch 22: the FTS path no longer calls snippet(). Its token window cut
// markdown markers in half (a "**Nexus" fragment survives stripping as
// literal asterisks) and its bracket markers duplicated the manual ones,
// so both paths now fetch the full row content and share the C++
// buildSearchSnippet() pipeline - one uniform snippet shape.
QVariantList SqliteMemory::searchMessages(const QString& keyword, int limit, bool includeArchived) {
    QVariantList out;
    const QString kw = keyword.trimmed();
    if (kw.isEmpty() || limit <= 0) return out;

    const bool ftsPath = kw.length() >= 3;
    QSqlQuery query(m_db);
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

    // Batch 22 (search-aggregate), pass 1/2: exact per-session aggregation.
    // GROUP BY counts every matching message row and records the session's
    // newest hit; `limit` now caps SESSIONS, so it is applied here. The
    // trailing session_id tie-break keeps the order deterministic when two
    // sessions share a newest-hit timestamp.
    if (ftsPath) {
        QString match = kw;
        match.replace(QLatin1String("\""), QLatin1String("\"\""));
        query.prepare(QStringLiteral(
            "SELECT m.session_id, s.title, COUNT(*), MAX(m.timestamp), "
            "COALESCE(s.archived, 0) "
            "FROM messages_fts JOIN messages m ON m.id = messages_fts.rowid "
            "LEFT JOIN sessions s ON s.id = m.session_id "
            "WHERE messages_fts MATCH ? AND (? OR COALESCE(s.archived, 0) = 0) "
            "AND COALESCE(s.deleted_at, 0) = 0 "
            "GROUP BY m.session_id "
            "ORDER BY MAX(m.timestamp) DESC, m.session_id DESC LIMIT ?"));
        query.addBindValue(QStringLiteral("\"%1\"").arg(match));
    } else {
        QString like = kw;
        like.replace(QLatin1String("\\"), QLatin1String("\\\\"))
            .replace(QLatin1String("%"), QLatin1String("\\%"))
            .replace(QLatin1String("_"), QLatin1String("\\_"));
        query.prepare(QStringLiteral(
            "SELECT m.session_id, s.title, COUNT(*), MAX(m.timestamp), "
            "COALESCE(s.archived, 0) "
            "FROM messages m LEFT JOIN sessions s ON s.id = m.session_id "
            "WHERE m.content LIKE ? ESCAPE '\\' AND (? OR COALESCE(s.archived, 0) = 0) "
            "AND COALESCE(s.deleted_at, 0) = 0 "
            "GROUP BY m.session_id "
            "ORDER BY MAX(m.timestamp) DESC, m.session_id DESC LIMIT ?"));
        query.addBindValue(QStringLiteral("%%1%").arg(like));
    }
    query.addBindValue(includeArchived);
    query.addBindValue(limit);
    if (!query.exec()) {
        qWarning() << "searchMessages failed:" << query.lastError().text();
        return out;
    }

    struct SessionHit {
        QString id;
        QString title;
        int hits = 0;
        qint64 latest = 0;
        bool archived = false;
    };
    QVector<SessionHit> sessions;
    while (query.next()) {
        sessions.append(SessionHit{query.value(0).toString(),
                                   query.value(1).toString(),
                                   query.value(2).toInt(),
                                   query.value(3).toLongLong(),
                                   query.value(4).toBool()});
    }
    if (sessions.isEmpty()) return out;

    // Pass 2/2: the best snippet row per session (a user-role hit first —
    // people usually search their own words, then the newest hit) PLUS the
    // session's newest hit message id (firstHitMessageId, for the sidebar's
    // jump-to-message). SQLite window functions cannot appear in WHERE, so
    // ROW_NUMBER() is computed in the inner SELECT and the outer filter keeps
    // the winning rows.
    //
    // Suggestion① (memory-peak): the window's intermediate result set no
    // longer selects `content` (the large text column). It carries only the
    // sort keys + message id; the winning row's role/content are fetched once
    // via a final JOIN onto that id. Two ROW_NUMBER()s — best_rn (user-first)
    // and latest_rn (newest hit) — share the single scan, so each session
    // contributes at most two rows (one when best == latest).
    QStringList placeholders;
    placeholders.reserve(sessions.size());
    for (int i = 0; i < sessions.size(); ++i)
        placeholders << QStringLiteral("?");
    const QString idList = placeholders.join(QStringLiteral(", "));
    QSqlQuery best(m_db);
    if (ftsPath) {
        QString match = kw;
        match.replace(QLatin1String("\""), QLatin1String("\"\""));
        best.prepare(QStringLiteral(
            "SELECT r.session_id, mb.role, mb.content, r.id, r.best_rn, r.latest_rn FROM ("
            "SELECT m.session_id AS session_id, m.id AS id, "
            "ROW_NUMBER() OVER (PARTITION BY m.session_id "
            "ORDER BY (m.role = 'user') DESC, m.timestamp DESC, m.id DESC) AS best_rn, "
            "ROW_NUMBER() OVER (PARTITION BY m.session_id "
            "ORDER BY m.timestamp DESC, m.id DESC) AS latest_rn "
            "FROM messages_fts JOIN messages m ON m.id = messages_fts.rowid "
            "LEFT JOIN sessions s ON s.id = m.session_id "
            "WHERE messages_fts MATCH ? AND (? OR COALESCE(s.archived, 0) = 0) "
            "AND COALESCE(s.deleted_at, 0) = 0 "
            "AND m.session_id IN (%1)) r "
            "JOIN messages mb ON mb.id = r.id "
            "WHERE r.best_rn = 1 OR r.latest_rn = 1").arg(idList));
        best.addBindValue(QStringLiteral("\"%1\"").arg(match));
    } else {
        QString like = kw;
        like.replace(QLatin1String("\\"), QLatin1String("\\\\"))
            .replace(QLatin1String("%"), QLatin1String("\\%"))
            .replace(QLatin1String("_"), QLatin1String("\\_"));
        best.prepare(QStringLiteral(
            "SELECT r.session_id, mb.role, mb.content, r.id, r.best_rn, r.latest_rn FROM ("
            "SELECT m.session_id AS session_id, m.id AS id, "
            "ROW_NUMBER() OVER (PARTITION BY m.session_id "
            "ORDER BY (m.role = 'user') DESC, m.timestamp DESC, m.id DESC) AS best_rn, "
            "ROW_NUMBER() OVER (PARTITION BY m.session_id "
            "ORDER BY m.timestamp DESC, m.id DESC) AS latest_rn "
            "FROM messages m LEFT JOIN sessions s ON s.id = m.session_id "
            "WHERE m.content LIKE ? ESCAPE '\\' AND (? OR COALESCE(s.archived, 0) = 0) "
            "AND COALESCE(s.deleted_at, 0) = 0 "
            "AND m.session_id IN (%1)) r "
            "JOIN messages mb ON mb.id = r.id "
            "WHERE r.best_rn = 1 OR r.latest_rn = 1").arg(idList));
        best.addBindValue(QStringLiteral("%%1%").arg(like));
    }
    best.addBindValue(includeArchived);
    for (const SessionHit& s : sessions)
        best.addBindValue(s.id);

    struct BestRow {
        QString role;
        QString content;
    };
    QHash<QString, BestRow> bestBySession;
    QHash<QString, qint64> firstHitBySession;
    if (!best.exec()) {
        // Pass 1 already proved the WHERE clause runs, so this is
        // unexpected; still return the aggregated session rows (empty
        // snippets) rather than dropping results the sidebar could show.
        qWarning() << "searchMessages (best-hit pass) failed:"
                   << best.lastError().text();
    } else {
        while (best.next()) {
            const QString sid = best.value(0).toString();
            // best_rn == 1 → the snippet row (user-first); latest_rn == 1 →
            // the newest hit message id (firstHitMessageId). When the two
            // windows agree on the same row (best == latest), both are
            // captured from this single row.
            // QHash::insert is idempotent for duplicate keys: when best_rn
            // and latest_rn both resolve to the same session (the same row
            // wins both windows), the second insert silently overwrites with
            // identical values — no dedup branch needed, and the final
            // mapping is correct either way.
            if (best.value(4).toInt() == 1) {
                bestBySession.insert(sid, BestRow{best.value(1).toString(),
                                                  best.value(2).toString()});
            }
            if (best.value(5).toInt() == 1) {
                firstHitBySession.insert(sid, best.value(3).toLongLong());
            }
        }
    }

    for (const SessionHit& s : sessions) {
        QVariantMap row;
        row.insert(QStringLiteral("sessionId"), s.id);
        row.insert(QStringLiteral("sessionTitle"), s.title);
        row.insert(QStringLiteral("hitCount"), s.hits);
        row.insert(QStringLiteral("latestTimestamp"), s.latest);
        row.insert(QStringLiteral("archived"), s.archived);
        const auto it = bestBySession.constFind(s.id);
        if (it != bestBySession.constEnd()) {
            row.insert(QStringLiteral("bestSnippet"),
                       buildSearchSnippet(it->content, kw));
            row.insert(QStringLiteral("bestRole"), it->role);
        } else {
            row.insert(QStringLiteral("bestSnippet"), QString());
            row.insert(QStringLiteral("bestRole"), QString());
        }
        const auto fh = firstHitBySession.constFind(s.id);
        row.insert(QStringLiteral("firstHitMessageId"),
                   fh != firstHitBySession.constEnd() ? fh.value() : qint64(0));
        out.append(row);
    }
    return out;
}

qint64 SqliteMemory::saveEpisodic(const QString& sessionId, const QJsonObject& episode) {
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO episodic (session_id, summary, tags, embedding, timestamp) "
                  "VALUES (?, ?, ?, ?, ?)");
    query.addBindValue(sessionId);
    query.addBindValue(episode.value("summary").toString());
    query.addBindValue(episode.value("tags").toString());
    QByteArray embBlob;
    const QJsonValue embVal = episode.value("embedding");
    if (embVal.isString())
        embBlob = QByteArray::fromBase64(embVal.toString().toLatin1());
    query.addBindValue(embBlob);
    query.addBindValue(QDateTime::currentDateTime().toMSecsSinceEpoch());

    if (!query.exec()) {
        qWarning() << "Failed to save episodic memory:" << query.lastError().text();
        return 0;
    }
    return query.lastInsertId().toLongLong();
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
        // docs/25 B2: the sub-trigram LIKE fallback cannot use any index
        // (leading wildcard) and full-scans the episodic table. Guard it
        // with the same row-ceiling discipline as searchMessages: above the
        // ceiling the scan is suppressed (empty result + warning) instead
        // of stalling the GUI thread; the warning log directs users at 3+
        // character keywords (which use the trigram FTS index).
        QSqlQuery countQuery(m_db);
        if (countQuery.exec(QStringLiteral("SELECT COUNT(*) FROM episodic"))
                && countQuery.next()
                && countQuery.value(0).toLongLong() > kLikeScanMaxEpisodic) {
            qWarning() << "searchEpisodic: LIKE fallback suppressed - episodic"
                          " table exceeds" << kLikeScanMaxEpisodic << "rows";
            return results;
        }
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

QJsonArray SqliteMemory::searchEpisodicSemantic(const QVector<float>& queryEmbedding, int limit) {
    QJsonArray results;
    if (queryEmbedding.isEmpty() || limit <= 0) return results;

    QSqlQuery query(m_db);
    // docs/25 B2: the LIMIT is a named constant now (kSemanticCandidateCap);
    // B1's idx_episodic_ts backs the ORDER BY so the scan reads at most the
    // newest cap rows instead of sorting the whole table.
    query.prepare("SELECT id, session_id, summary, tags, timestamp, embedding FROM episodic "
                  "WHERE length(embedding) > 0 ORDER BY timestamp DESC LIMIT ?");
    query.addBindValue(kSemanticCandidateCap);
    if (!query.exec()) {
        qWarning() << "searchEpisodicSemantic failed:" << query.lastError().text();
        return results;
    }

    float queryNorm = 0.0f;
    for (float v : queryEmbedding) queryNorm += v * v;
    queryNorm = std::sqrt(queryNorm);
    if (queryNorm < 1e-6f) return results;

    struct Candidate { QJsonObject episode; float similarity; };
    QVector<Candidate> candidates;

    int scanned = 0;
    while (query.next()) {
        ++scanned;
        const QByteArray embBlob = query.value(5).toByteArray();
        if (embBlob.isEmpty()) continue;
        const QVector<float> emb = embeddingFromBlob(embBlob);
        if (emb.size() != queryEmbedding.size()) continue;

        float dot = 0.0f, norm = 0.0f;
        for (int i = 0; i < emb.size(); ++i) {
            dot += queryEmbedding[i] * emb[i];
            norm += emb[i] * emb[i];
        }
        norm = std::sqrt(norm);
        if (norm < 1e-6f) continue;

        QJsonObject episode;
        episode["id"] = query.value(0).toInt();
        episode["session_id"] = query.value(1).toString();
        episode["summary"] = query.value(2).toString();
        episode["tags"] = query.value(3).toString();
        episode["timestamp"] = query.value(4).toLongLong();
        // docs/25 B2: carry the cosine score so EpisodicService can merge
        // FTS and semantic hits by a unified relevance score.
        episode["score"] = double(dot / (queryNorm * norm));
        candidates.append({episode, dot / (queryNorm * norm)});
    }

    // docs/25 B2: a full candidate set means older episodes were truncated
    // away — surface it loudly instead of silently forgetting them.
    if (scanned >= kSemanticCandidateCap)
        qWarning() << "searchEpisodicSemantic: candidate cap" << kSemanticCandidateCap
                   << "reached - older episodes are not considered this round";

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.similarity > b.similarity; });

    const int n = qMin(limit, candidates.size());
    for (int i = 0; i < n; ++i)
        results.append(candidates[i].episode);

    return results;
}

bool SqliteMemory::updateEpisodicEmbedding(qint64 id, const QByteArray& blob) {
    if (id <= 0) return false;
    QSqlQuery query(m_db);
    query.prepare("UPDATE episodic SET embedding = ? WHERE id = ?");
    query.addBindValue(blob);
    query.addBindValue(id);
    if (!query.exec()) {
        qWarning() << "updateEpisodicEmbedding failed:" << query.lastError().text();
        return false;
    }
    return query.numRowsAffected() > 0;
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
    const auto execDelete = [&](const char* what) {
        if (!query.exec()) {
            qWarning() << "Failed to" << what << ":" << query.lastError().text();
            return false;
        }
        return true;
    };

    // docs/25 B4: the three DELETEs run in ONE transaction (same discipline
    // as truncateLastTurn) — a mid-way failure used to leave the session
    // half-cleared with no way to retry the missing part coherently. A
    // failed statement rolls the whole clear back; when a transaction
    // cannot be started (already inside one), fall back to the legacy
    // statement-by-statement path so the operation never no-ops.
    if (m_db.transaction()) {
        query.prepare("DELETE FROM messages WHERE session_id = ?");
        query.addBindValue(sessionId);
        bool ok = execDelete("clear messages");

        if (ok) {
            query.prepare("DELETE FROM episodic WHERE session_id = ?");
            query.addBindValue(sessionId);
            ok = execDelete("clear episodic memory");
        }

        // docs/23 G6: align with deleteSession's cascade — leaving trace nodes
        // behind would resurrect ghost trajectories when the session reopens.
        if (ok) {
            query.prepare("DELETE FROM trace_nodes WHERE session_id = ?");
            query.addBindValue(sessionId);
            ok = execDelete("clear trace nodes");
        }

        if (ok && m_db.commit()) return;
        qWarning() << "clear failed; rolling back:" << query.lastError().text();
        m_db.rollback();
        return;
    }

    query.prepare("DELETE FROM messages WHERE session_id = ?");
    query.addBindValue(sessionId);
    execDelete("clear messages");

    query.prepare("DELETE FROM episodic WHERE session_id = ?");
    query.addBindValue(sessionId);
    execDelete("clear episodic memory");

    query.prepare("DELETE FROM trace_nodes WHERE session_id = ?");
    query.addBindValue(sessionId);
    execDelete("clear trace nodes");
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

bool SqliteMemory::setSessionPinned(const QString& id, bool pinned) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE sessions SET pinned = ? WHERE id = ?");
    query.addBindValue(pinned);
    query.addBindValue(id);
    return query.exec() && query.numRowsAffected() == 1;
}

bool SqliteMemory::setSessionArchived(const QString& id, bool archived) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE sessions SET archived = ? WHERE id = ?");
    query.addBindValue(archived);
    query.addBindValue(id);
    return query.exec() && query.numRowsAffected() == 1;
}

QJsonArray SqliteMemory::listSessions(int limit, bool includeArchived) {
    QSqlQuery query(m_db);
    query.prepare("SELECT id, title, updated_at, project_id, pinned, archived FROM sessions "
                  "WHERE (? OR archived = 0) AND COALESCE(deleted_at, 0) = 0 "
                  "ORDER BY pinned DESC, updated_at DESC, id DESC LIMIT ?");
    query.addBindValue(includeArchived);
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
            session["pinned"] = query.value(4).toBool();
            session["archived"] = query.value(5).toBool();
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

// docs/23 G7: 软删除。消息/轨迹/审计为撤销窗口保留（审计链"重新生成是
// 正常行为"的同一纪律），列表与搜索按 deleted_at 过滤；到期行由
// purgeDeletedSessions 硬级联回收。
void SqliteMemory::deleteSession(const QString& sessionId) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE sessions SET deleted_at = ? WHERE id = ?");
    query.addBindValue(QDateTime::currentDateTime().toMSecsSinceEpoch());
    query.addBindValue(sessionId);
    if (!query.exec())
        qWarning() << "Failed to delete session:" << query.lastError().text();
}

bool SqliteMemory::restoreSession(const QString& sessionId) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE sessions SET deleted_at = NULL WHERE id = ?");
    query.addBindValue(sessionId);
    return query.exec() && query.numRowsAffected() == 1;
}

int SqliteMemory::purgeDeletedSessions(qint64 deletedBefore) {
    QSqlQuery query(m_db);
    query.prepare("SELECT id FROM sessions "
                  "WHERE deleted_at IS NOT NULL AND deleted_at > 0 AND deleted_at < ?");
    query.addBindValue(deletedBefore);
    if (!query.exec()) {
        qWarning() << "Failed to list expired sessions:"
                   << query.lastError().text();
        return 0;
    }
    QStringList expired;
    while (query.next()) expired.append(query.value(0).toString());
    if (expired.isEmpty()) return 0;

    // docs/25 B4: the whole purge round runs in ONE transaction. Purging
    // per session (four auto-committed DELETEs each) left a crash window
    // with orphaned rows and held write locks far longer on large purges.
    // Any failed statement aborts the round via the commit check below.
    if (m_db.transaction()) {
        bool ok = true;
        for (const QString& id : std::as_const(expired))
            ok = hardDeleteSessionRows(id) && ok;
        if (ok && m_db.commit()) return expired.size();
        qWarning() << "purgeDeletedSessions failed; rolling back:"
                   << m_db.lastError().text();
        m_db.rollback();
        return 0;
    }

    // Legacy fallback: no transaction possible (already inside one) —
    // purge session by session, matching the pre-B4 behavior.
    int purged = 0;
    for (const QString& id : std::as_const(expired)) {
        hardDeleteSessionRows(id);
        ++purged;
    }
    return purged;
}

bool SqliteMemory::hardDeleteSessionRows(const QString& sessionId) {
    QSqlQuery query(m_db);
    // docs/25 B4: propagate failure to the caller (purgeDeletedSessions
    // rolls the whole transaction back on the first failure).
    bool ok = true;
    const auto execDelete = [&](const char* what) {
        if (!query.exec()) {
            qWarning() << "Failed to" << what << ":" << query.lastError().text();
            ok = false;
        }
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
    return ok;
}

// ── Trace view persistence ───────────────────────────────

void SqliteMemory::saveTraceNode(const QString& sessionId, const QJsonObject& node, int order) {
    QSqlQuery query(m_db);
    // Upsert: position (node_order) is fixed by the first insert; later
    // saves of the same node id only refresh content/status.
    query.prepare("INSERT INTO trace_nodes (session_id, node_id, node_order, type, title, "
                  "status, args, result, reasoning, timestamp, message_id, metadata, workspace_id) "
                  "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 'local') "
                  "ON CONFLICT(session_id, node_id) DO UPDATE SET type = excluded.type, "
                  "title = excluded.title, status = excluded.status, args = excluded.args, "
                  "result = excluded.result, reasoning = excluded.reasoning, "
                  "timestamp = excluded.timestamp, message_id = excluded.message_id, "
                  "metadata = excluded.metadata");
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
    query.addBindValue(nz(node, "reasoning"));
    query.addBindValue(static_cast<qint64>(node.value("timestamp").toDouble()));
    query.addBindValue(node.value("messageId").toVariant().toLongLong());
    QJsonObject metadata;
    for (const char* key : {"images", "files", "model", "preview"})
        if (node.contains(QLatin1String(key))) metadata[key] = node.value(QLatin1String(key));
    query.addBindValue(QString::fromUtf8(QJsonDocument(metadata).toJson(QJsonDocument::Compact)));

    if (!query.exec()) {
        qWarning() << "Failed to save trace node:" << query.lastError().text();
    }
}

namespace {

// trace_nodes 的一行 → 节点对象。列映射表不是整节点 JSON，所以读端要重建形状；
// 白名单字段（images/files/model）从 metadata 提升回顶层，messageId 来自列。
QJsonObject traceNodeFromRow(const QSqlQuery& query) {
    QJsonObject node;
    node["id"] = query.value(0).toString();
    node["type"] = query.value(1).toString();
    node["title"] = query.value(2).toString();
    node["status"] = query.value(3).toString();
    node["args"] = query.value(4).toString();
    node["result"] = query.value(5).toString();
    // 空值不写键：与运行期 appendTraceNode 的白名单形状保持一致，
    // 否则恢复出来的节点会多出一个 reasoning: ""。
    const QString reasoning = query.value(6).toString();
    if (!reasoning.isEmpty())
        node["reasoning"] = reasoning;
    node["timestamp"] = query.value(7).toLongLong();
    node["order"] = query.value(8).toInt();
    const qint64 messageId = query.value(9).isNull() ? 0 : query.value(9).toLongLong();
    if (messageId > 0)
        node["messageId"] = messageId;
    const auto metadata = QJsonDocument::fromJson(query.value(10).toByteArray()).object();
    for (const char* key : {"images", "files", "model", "preview"})
        if (metadata.contains(QLatin1String(key))) node[key] = metadata.value(QLatin1String(key));
    return node;
}

constexpr const char* kTraceNodeColumns =
    "SELECT node_id, type, title, status, args, result, reasoning, timestamp, "
    "node_order, message_id, metadata FROM trace_nodes ";

} // namespace

QJsonArray SqliteMemory::loadTraceNodes(const QString& sessionId) {
    QSqlQuery query(m_db);
    query.prepare(QString(kTraceNodeColumns) + "WHERE session_id = ? ORDER BY node_order ASC");
    query.addBindValue(sessionId);

    QJsonArray nodes;
    if (query.exec()) {
        while (query.next())
            nodes.append(traceNodeFromRow(query));
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

// docs/23 G3: time-based retention for the two audit-domain tables that
// previously had none. Both DELETEs ride their existing timestamp indexes
// (idx_audit_ts / idx_token_usage_ts); keepDays <= 0 keeps rows forever.
int SqliteMemory::pruneAudit(int keepDays) {
    if (keepDays <= 0) return 0;
    QSqlQuery query(m_db);
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch()
                        - static_cast<qint64>(keepDays) * 86400 * 1000;
    query.prepare("DELETE FROM audit_log WHERE timestamp < ?");
    query.addBindValue(cutoff);
    if (!query.exec()) {
        qWarning() << "Failed to prune audit log:" << query.lastError().text();
        return 0;
    }
    return query.numRowsAffected();
}

int SqliteMemory::pruneTokenUsage(int keepDays) {
    if (keepDays <= 0) return 0;
    QSqlQuery query(m_db);
    const qint64 cutoff = QDateTime::currentMSecsSinceEpoch()
                        - static_cast<qint64>(keepDays) * 86400 * 1000;
    query.prepare("DELETE FROM token_usage WHERE timestamp < ?");
    query.addBindValue(cutoff);
    if (!query.exec()) {
        qWarning() << "Failed to prune token usage:" << query.lastError().text();
        return 0;
    }
    return query.numRowsAffected();
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
    // A8: truncate tool arguments before binding — a model can pass
    // arbitrarily long arguments that would bloat the database.
    static constexpr int kMaxAuditArgsChars = 10000;
    QString boundedArgs = arguments.isNull() ? QString() : arguments;
    if (boundedArgs.size() > kMaxAuditArgsChars)
        boundedArgs = boundedArgs.left(kMaxAuditArgsChars)
            + QStringLiteral("...[truncated]");
    query.addBindValue(boundedArgs);
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

// ── docs/23 D1: database backup ─────────────────────────────────

QString SqliteMemory::defaultBackupsDir() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/backups");
}

bool SqliteMemory::createBackupFile(const QString& dbPath, const QString& targetPath) {
    // Self-contained connection: created, used and dropped on the calling
    // thread (Qt SQL rule) — the GUI-thread instances are never touched.
    const QString connName = QStringLiteral("nexus_backup_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
        db.setDatabaseName(dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!db.open()) {
            qWarning() << "Backup: cannot open source database:"
                       << db.lastError().text();
            QSqlDatabase::removeDatabase(connName);
            return false;
        }
        QSqlQuery query(db);
        query.setForwardOnly(true);
        // busy_timeout guards the rare overlap with a GUI-thread write;
        // under WAL the snapshot's read transaction never blocks it.
        query.exec("PRAGMA busy_timeout = 5000");
        query.prepare("VACUUM INTO ?");
        query.addBindValue(QDir::toNativeSeparators(targetPath));
        if (!query.exec()) {
            qWarning() << "Backup failed (VACUUM INTO):"
                       << query.lastError().text();
            db.close();
            QSqlDatabase::removeDatabase(connName);
            // A half-written snapshot is worse than none.
            QFile::remove(targetPath);
            return false;
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(connName);
    return true;
}

int SqliteMemory::rotateAutoBackups(const QString& backupsDir, int keepCount) {
    if (keepCount <= 0) return 0;
    QDir dir(backupsDir);
    const QFileInfoList autos = dir.entryInfoList(
        {QStringLiteral("auto_*.db")}, QDir::Files);
    // Explicit newest-first sort by name: the timestamps inside the file
    // names sort lexically by age (manual_* snapshots are never matched).
    QFileInfoList sorted = autos;
    std::sort(sorted.begin(), sorted.end(),
              [](const QFileInfo& a, const QFileInfo& b) {
                  return a.fileName() > b.fileName();
              });
    int removed = 0;
    for (int i = keepCount; i < sorted.size(); ++i) {
        if (QFile::remove(sorted.at(i).absoluteFilePath())) ++removed;
    }
    return removed;
}

QVariantMap SqliteMemory::inspectBackupFile(const QString& backupPath) {
    QVariantMap out;
    if (!QFileInfo(backupPath).isFile()) {
        out[QStringLiteral("error")] = QStringLiteral("file not found");
        return out;
    }
    const QString connName = QStringLiteral("nexus_backup_preview_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
        db.setDatabaseName(backupPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!db.open()) {
            out[QStringLiteral("error")] = db.lastError().text();
            QSqlDatabase::removeDatabase(connName);
            return out;
        }
        QSqlQuery query(db);
        query.setForwardOnly(true);
        const auto scalar = [&query](const char* sql, qint64* value) {
            if (!query.exec(QString::fromLatin1(sql)) || !query.next())
                return false;
            *value = query.value(0).toLongLong();
            return true;
        };
        qint64 sessions = 0, messages = 0, docs = 0, newest = 0;
        scalar("SELECT count(*) FROM sessions", &sessions);
        scalar("SELECT count(*) FROM messages", &messages);
        scalar("SELECT count(*) FROM knowledge_documents", &docs);
        scalar("SELECT COALESCE(MAX(timestamp), 0) FROM messages", &newest);
        out[QStringLiteral("sessions")] = sessions;
        out[QStringLiteral("messages")] = messages;
        out[QStringLiteral("knowledgeDocs")] = docs;
        out[QStringLiteral("newestTimestamp")] = double(newest);
        // A corrupt snapshot must be visible in the preview, not on
        // restore day.
        query.exec("PRAGMA quick_check");
        out[QStringLiteral("integrityOk")] =
            query.next() && query.value(0).toString() == QLatin1String("ok");
        db.close();
    }
    QSqlDatabase::removeDatabase(connName);
    return out;
}

// docs/24 R1: restore-time integrity gate for a candidate snapshot.
bool SqliteMemory::snapshotPassesQuickCheck(const QString& path) {
    const QString connName = QStringLiteral("nexus_restore_check_%1")
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
    bool ok = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery query(db);
            query.setForwardOnly(true);
            ok = query.exec("PRAGMA quick_check")
                 && query.next()
                 && query.value(0).toString() == QLatin1String("ok");
            if (!ok)
                qWarning() << "Restore: snapshot failed quick_check:"
                           << query.lastError().text();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connName);
    return ok;
}

// docs/24 R1: swap the live database file for a snapshot — the D2 restore
// primitive. Precondition: no other open connection to the database file
// (the UI flow closes all tabs first; the single-instance lock excludes
// other processes). Every failure path restores the original file and
// reopens it, so the instance is always left usable.
bool SqliteMemory::restoreFromBackup(const QString& backupPath) {
    const QString dbPath = m_dbPath;
    const QString tmpPath = dbPath + QStringLiteral(".restore-tmp");
    const QString rollbackPath = dbPath + QStringLiteral(".pre-restore");

    // ── 1. Close our own connection. Closing the LAST connection to a WAL
    // database checkpoints and removes the -wal sidecar, so the bare .db
    // file is complete from here on (rollback anchor copies are faithful).
    if (m_db.isOpen()) m_db.close();
    m_db = QSqlDatabase();   // drop the handle before removeDatabase (MEM-702)
    if (!m_connName.isEmpty()) {
        QSqlDatabase::removeDatabase(m_connName);
        m_connName.clear();
    }

    // Failure helper: best-effort reopen of the original (rolling back via
    // the anchor when the swap already happened) and report failure.
    const auto failAndReopen = [&](const char* what) {
        qWarning() << "Database restore failed at" << what;
        QFile::remove(tmpPath);
        if (QFile::exists(rollbackPath) && !QFile::exists(dbPath)) {
            // Swap already happened: roll the anchor back over the file.
            QFile::remove(dbPath + "-wal");
            QFile::remove(dbPath + "-shm");
            if (!QFile::copy(rollbackPath, dbPath))
                qWarning() << "Database restore: ROLLBACK copy failed —"
                           << "original snapshot kept at" << rollbackPath;
        }
        if (!openConnection())
            qCritical() << "Database restore: reopen failed — restart required";
        return false;
    };

    // ── 2. Candidate snapshot must pass integrity before anything moves.
    if (!snapshotPassesQuickCheck(backupPath))
        return failAndReopen("integrity check");

    // ── 3. Rollback anchor: the current file, before it is touched.
    QFile::remove(rollbackPath);
    if (!QFile::copy(dbPath, rollbackPath))
        return failAndReopen("rollback anchor");

    // ── 4. Stage the snapshot next to the live file.
    QFile::remove(tmpPath);
    if (!QFile::copy(backupPath, tmpPath))
        return failAndReopen("stage snapshot");

    // ── 5. Sidecars of the OLD database must not survive the swap.
    QFile::remove(dbPath + "-wal");
    QFile::remove(dbPath + "-shm");

    // ── 6. Swap: remove + rename (QFile::rename fails onto an existing
    // target, so the two-step form is mandatory).
    if (!QFile::remove(dbPath) || !QFile::rename(tmpPath, dbPath))
        return failAndReopen("swap");

    // ── 7. Reopen on the restored file.
    if (!openConnection()) {
        qWarning() << "Database restore: restored file failed to open —"
                   << "rolling back";
        m_db = QSqlDatabase();
        if (!m_connName.isEmpty()) {
            QSqlDatabase::removeDatabase(m_connName);
            m_connName.clear();
        }
        QFile::remove(dbPath);
        QFile::copy(rollbackPath, dbPath);
        if (!openConnection())
            qCritical() << "Database restore: rollback reopen failed —"
                        << "restart required";
        return false;
    }

    // ── 8. Success: the anchor has served its purpose.
    QFile::remove(rollbackPath);
    qInfo() << "Database restored from" << backupPath;
    return true;
}

} // namespace NexusAI
