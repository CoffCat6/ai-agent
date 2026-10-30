#pragma once

#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>

namespace NexusAI {

// Portable, credential-free settings exchange. SQLite data is intentionally
// outside this format; restored settings take effect after an application restart.
class SettingsBackup : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(SettingsBackup)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged FINAL)
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY restartRequiredChanged FINAL)
public:
    explicit SettingsBackup(QObject* parent = nullptr);
    static SettingsBackup* instance();
    static SettingsBackup* create(QQmlEngine*, QJSEngine*);

    QString lastError() const { return m_lastError; }
    bool restartRequired() const { return m_restartRequired; }

    Q_INVOKABLE bool exportSettings(const QString& path);
    Q_INVOKABLE QVariantMap previewImport(const QString& path);
    Q_INVOKABLE bool importSettings(const QString& path);
    // 诊断包（日志与诊断页）：在 directory 下新建 nexus-diagnostics-…/
    // 目录，放入脱敏设置快照、日志副本与环境信息。目录路径不像 JSON 那样
    // 好随手挑，所以整体作为一次操作完成；成功返回该目录的绝对路径，
    // 失败返回空串并设置 lastError。
    Q_INVOKABLE QString exportDiagnostics(const QString& directory);

signals:
    void lastErrorChanged();
    void restartRequiredChanged();

private:
    // 构建可移植、脱敏的设置备份根对象（导出设置与诊断包共用同一套
    // 过滤规则）；失败时返回空对象并已设置 lastError。
    QJsonObject buildExportPayload();
    void setError(const QString& error);
    QVariantMap parseImport(const QString& path, QVariantMap* settings,
                            QVariantList* profiles, QVariantList* servers);

    QString m_lastError;
    bool m_restartRequired{false};
    static SettingsBackup* s_instance;
};

} // namespace NexusAI
