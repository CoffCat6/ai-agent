#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QVariantMap>

namespace NexusAI {

// docs/31 S4.6 N8：可分享预设包。
//
// 与 SettingsBackup 的分工：备份是"整机设置快照"（恢复需重启、含分类过滤与
// 逐键差异），预设包是"可分享的起步配置"——只带三组可移植资产（系统提示
// 预设 / 斜杠指令 / 工具启用状态 + 默认权限模式），导入后立即生效、无需重启。
// 结构里没有任何密钥、模型配置、MCP 凭据、会话模板或按项目覆盖。
class PresetPackTransfer : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_NAMED_ELEMENT(PresetPackTransfer)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged FINAL)
public:
    explicit PresetPackTransfer(QObject* parent = nullptr);
    static PresetPackTransfer* instance();
    static PresetPackTransfer* create(QQmlEngine*, QJSEngine*);

    QString lastError() const { return m_lastError; }

    Q_INVOKABLE bool exportPack(const QString& path);
    // 只读预览：valid/error、四组计数、同名冲突清单（≤200 条 + conflictsTotal）、
    // 将发生的变化 changes 与 warnings；不改动任何本机设置。
    Q_INVOKABLE QVariantMap previewPack(const QString& path);
    // conflictPolicy: overwrite / skip / rename。只用于"同名条目"（预设名、
    // 指令词，大小写不敏感）；禁用工具与默认权限模式是单值配置，始终按包
    // 内容应用（预览会明确列出）。任一步写盘失败都会回滚已应用的部分。
    Q_INVOKABLE bool importPack(const QString& path, const QString& conflictPolicy);

signals:
    void lastErrorChanged();

private:
    struct Pack {
        QJsonArray presets;      // [{name, prompt}]
        QJsonArray commands;     // [{name, templ, note}]
        QStringList disabledTools;
        QString permissionMode;  // 空 = 不修改
    };
    // 解析并校验预设包；失败返回 {valid:false,error}，成功返回 {valid:true}。
    QVariantMap parsePack(const QString& path, Pack* pack);
    void setError(const QString& error);

    QString m_lastError;
    static PresetPackTransfer* s_instance;
};

} // namespace NexusAI
