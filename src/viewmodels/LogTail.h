#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>

namespace NexusAI {

// docs/31 O6：日志页「最近日志」的 QML 入口。刻意独立于 AgentVM——
// 它只读本地日志文件，不碰会话/数据库，也就不需要启动 agent 栈。
class LogTail : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(LogTail)
public:
    explicit LogTail(QObject* parent = nullptr);
    static LogTail* instance();
    static LogTail* create(QQmlEngine*, QJSEngine*);

    // path 为空 = 当前日志（Logger::logFilePath()）或日志目录里最新的文件。
    // maxLines 夹紧到 1..2000；minLevel 为空 = 全部，否则是
    // debug/info/warning/critical（含该级别）。返回
    // {ok, path, error, lines, matched, truncated, bytesRead}。
    Q_INVOKABLE QVariantMap read(const QString& path, int maxLines,
                                 const QString& minLevel,
                                 const QString& keyword) const;
};

} // namespace NexusAI
