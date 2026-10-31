#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>

namespace NexusAI {

// docs/31 N10：关于页的构建信息与数据目录。
//
// 这些值全部是进程级常量（Qt 版本、编译器、CPU 架构、数据目录），
// 单独成单例，避免继续往 SettingsService 里塞 UI 信息；QML 侧只读，
// 唯一的动作是 openDataDir()。
class AppInfo : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(AppInfo)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT FINAL)
    Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT FINAL)
    Q_PROPERTY(QString compiler READ compiler CONSTANT FINAL)
    Q_PROPERTY(QString architecture READ architecture CONSTANT FINAL)
    Q_PROPERTY(QString osVersion READ osVersion CONSTANT FINAL)
    Q_PROPERTY(QString dataDir READ dataDir CONSTANT FINAL)
    // 运行时依赖的第三方组件清单（名称 — 许可证），数据目录本身不含
    // 许可全文，故只做展示，不提供"打开许可文件"。
    Q_PROPERTY(QStringList licenses READ licenses CONSTANT FINAL)

public:
    explicit AppInfo(QObject* parent);

    static AppInfo* instance();
    static AppInfo* create(QQmlEngine*, QJSEngine*);

    QString appVersion() const;
    QString qtVersion() const;
    QString compiler() const;
    QString architecture() const;
    QString osVersion() const;
    QString dataDir() const;
    QStringList licenses() const;

    // 打开应用数据目录（不存在则创建）；返回 false 表示目录为空或
    // 系统拒绝打开，页面据此展示提示。
    Q_INVOKABLE bool openDataDir() const;

private:
    static AppInfo* s_instance;
};

} // namespace NexusAI
