#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace NexusAI {
namespace McpConfigTransfer {

// docs/31 N7：MCP 服务器配置的独立导入/导出文件（与整体设置备份分开）。
//
// 设计要点：
//  - 导出只写白名单字段（name/transport/command/args/url），env/headers
//    默认不写，enabled 一律 false；文件里用 secretsOmitted 记录有几台
//    服务器因此丢了凭据，导入页/导出提示据此告知用户。
//  - 导入保留文件里的 env/headers（用户主动选的配置文件，MCP 凭据本就
//    存在本地 mcp_servers.json），但导入后一律禁用，必须手动启用。
//  - 冲突策略只影响同名条目：覆盖（替换字段并保持禁用）/ 跳过 / 重命名。
enum class MergePolicy { Overwrite, Skip, Rename };

struct ImportEntry {
    QVariantMap server;   // 白名单字段（含 env/headers，若文件里有）
    bool conflict{false}; // 与现有服务器同名
    QString error;        // 非空 = 该条不合法，会跳过
};

struct ImportPreview {
    bool ok{false};
    QString error;        // 文件级错误（读不到/格式不对）
    int total{0};         // 文件里的条目数
    int importable{0};    // 合法条目数（含冲突）
    int conflictCount{0};
    int withSecrets{0};   // 含 env/headers 的条目数
    QVector<ImportEntry> entries;
};

// 文件结构：{"format":"nexusai-mcp-servers","version":1,
//           "servers":[{name,transport,command,args,url,enabled:false}],
//           "secretsOmitted":N,"omitted":["env","headers"]}
QJsonObject buildExport(const QVector<QVariantMap>& servers);
bool writeExport(const QString& path, const QVector<QVariantMap>& servers,
                 QString* error);

// 读取并校验文件；entries 按文件顺序，非法条目保留 error 供页面展示。
// current 用于标记同名冲突（覆盖/跳过/重命名策略只对冲突项生效）。
ImportPreview previewImportFile(const QString& path,
                                const QVector<QVariantMap>& current = {});

// 纯函数：把预览结果合并进当前配置，返回新列表。added/replaced/skipped
// 可为空。重命名策略用 "<name> (2)"、" (3)"… 保证唯一。
QVector<QVariantMap> applyImport(const QVector<QVariantMap>& current,
                                 const ImportPreview& preview,
                                 MergePolicy policy, int* added = nullptr,
                                 int* replaced = nullptr,
                                 int* skipped = nullptr);

// "overwrite"/"skip"/"rename"；无法识别时回退 Skip（绝不默默覆盖）。
MergePolicy policyFromName(const QString& name);
QString policyName(MergePolicy policy);

} // namespace McpConfigTransfer
} // namespace NexusAI
