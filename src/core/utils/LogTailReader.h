#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace NexusAI {

// docs/31 O6：日志页「最近日志」的只读文件尾部接口。
//
// 只读文件尾部固定字节数（默认 1 MB），因此超大日志（例如 1 GB）不会
// 整个读进内存；过滤只在尾部块上完成，返回最近 maxLines 条匹配行。
// 级别阈值沿用 Logger::severityRank：-1 = 全部，0=Debug … 4=Fatal。
// 没有级别前缀的续行（异常堆栈等）继承上一行解析出的级别。
struct LogTailOptions {
    int maxLines{200};        // 实际夹紧到 1..2000
    int minSeverityRank{-1};  // -1 = 全部；0..4 = Debug..Fatal（含该级别）
    QString keyword;          // 大小写不敏感子串；空 = 不过滤
    qint64 maxBytes{1024 * 1024};
    int maxLineLength{2000};  // 超长行截断，避免单行撑爆 UI
};

struct LogTailResult {
    bool ok{false};
    QString path;
    QString error;
    QStringList lines;      // 最近的匹配行，保持文件顺序（旧→新）
    int matched{0};         // 本次尾部块中的匹配行数
    bool truncated{false};  // 窗口前还有未读内容，或匹配行超过 maxLines
    qint64 bytesRead{0};
};

// path 为空时依次尝试 Logger::logFilePath() 与日志目录里最新的
// nexus-*.log。文件不存在/不可读时 ok=false 并填 error。
LogTailResult readLogTail(const QString& path, const LogTailOptions& options);

// 日志目录中最近修改的 nexus-*.log；没有时返回空串。
QString newestLogFile();

} // namespace NexusAI
