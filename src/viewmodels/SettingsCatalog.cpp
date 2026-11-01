#include "SettingsCatalog.h"

#include "core/utils/SettingsSchema.h"
#include "EditBuffer.h"
#include "PreferenceRegistry.h"
#include "PreferenceStore.h"
#include "SettingsService.h"
#include "SettingsValueFormat.h"

#include <QCoreApplication>
#include <QJSEngine>
#include <QVector>
#include <algorithm>

namespace NexusAI {
namespace {

constexpr CatalogResetScope kNone = CatalogResetScope::None;
constexpr CatalogResetScope kPref = CatalogResetScope::Preference;
constexpr CatalogResetScope kPage = CatalogResetScope::Page;

#define NEXUS_CATALOG_ID(text) QStringLiteral(text)

// M1 键与页面级入口的静态登记表。
//
// 纪律：
//   * 每个 SettingsSchema::registeredKeys() 里的键都必须在这里出现（隐藏条目
//     也算），test_settings 会逐个断言；
//   * id 同时是 QML 目标的 objectName——页面在对应的 SettingRow/SettingsField/
//     SettingsSection/SettingCard 上写同名字符串即可被搜索跳转定位；
//   * 没有对应控件的条目（页级入口/内部键）不会命中锚点，跳转只打开分类。
const QVector<CatalogEntryDef>& staticEntries() {
    static const QVector<CatalogEntryDef> values = {
        // 设置中心导航自身的状态：不是用户可调的设置，登记只为满足
        // 「每个 schema 键都要有条目」的覆盖断言。
        {NEXUS_CATALOG_ID("settings.collapsedGroups"), QString(),
         QT_TRANSLATE_NOOP("SettingsSearch", "导航分组折叠状态"),
         QStringLiteral("collapsedGroups settings navigation"), true, kNone},

        // ── 模型配置（页面按卡片分区，字段级锚点打在 SettingCard 上）──
        {NEXUS_CATALOG_ID("model.profile"), QStringLiteral("model"),
         QT_TRANSLATE_NOOP("SettingsSearch", "配置来源"),
         QStringLiteral("profile 配置 协议 厂商 新建 provider"), false, kNone},
        {NEXUS_CATALOG_ID("model.connection"), QStringLiteral("model"),
         QT_TRANSLATE_NOOP("SettingsSearch", "服务连接"),
         QStringLiteral("baseUrl api 地址 密钥 apiKey 连接 测试连接 endpoint"), false, kNone},
        {NEXUS_CATALOG_ID("model.models"), QStringLiteral("model"),
         QT_TRANSLATE_NOOP("SettingsSearch", "模型"),
         QStringLiteral("模型 上下文 context 窗口 模型列表 model"), false, kNone},
        {NEXUS_CATALOG_ID("model.generation"), QStringLiteral("model"),
         QT_TRANSLATE_NOOP("SettingsSearch", "生成参数"),
         QStringLiteral("温度 temperature 采样 top_p 生成 generation"), false, kNone},
        {NEXUS_CATALOG_ID("model.reasoning"), QStringLiteral("model"),
         QT_TRANSLATE_NOOP("SettingsSearch", "思考与上下文"),
         QStringLiteral("思考 推理 reasoning 思考强度 上下文长度 context"), false, kNone},
        {NEXUS_CATALOG_ID("modelProfiles"), QStringLiteral("model"),
         QT_TRANSLATE_NOOP("SettingsSearch", "模型配置数据"),
         QStringLiteral("modelProfiles profiles"), true, kNone},
        {NEXUS_CATALOG_ID("activeProfileId"), QStringLiteral("model"),
         QT_TRANSLATE_NOOP("SettingsSearch", "当前模型配置"),
         QStringLiteral("activeProfileId"), true, kNone},
        {NEXUS_CATALOG_ID("contextTokens"), QStringLiteral("model"),
         QT_TRANSLATE_NOOP("SettingsSearch", "上下文预算"),
         QStringLiteral("contextTokens 上下文 预算"), true, kNone},

        // ── 智能体行为 ──────────────────────────────────────
        {NEXUS_CATALOG_ID("agent/maxSteps"), QStringLiteral("agent"),
         QT_TRANSLATE_NOOP("SettingsSearch", "单次任务步数预算"),
         QStringLiteral("模型行为 步数 预算 超时 maxSteps timeout 任务上限 智能体 iteration"),
         false, kPage},
        {NEXUS_CATALOG_ID("agent/taskTimeoutMinutes"), QStringLiteral("agent"),
         QT_TRANSLATE_NOOP("SettingsSearch", "单次任务超时（分钟）"),
         QStringLiteral("超时 timeout 分钟 任务时长 智能体"), false, kPage},
        {NEXUS_CATALOG_ID("agent/systemPromptOverride"), QStringLiteral("agent"),
         QT_TRANSLATE_NOOP("SettingsSearch", "自定义系统提示词"),
         QStringLiteral("系统提示词 prompt override 提示词 人设"), false, kPage},
        // docs/32 P2: 子代理设置——随设置页落地转为可见条目。
        {NEXUS_CATALOG_ID("agent/subagentEnabled"), QStringLiteral("agent"),
         QT_TRANSLATE_NOOP("SettingsSearch", "子代理开关"),
         QStringLiteral("子代理 subagent spawn_agent 派生 并行"), false, kPage},
        {NEXUS_CATALOG_ID("agent/subagentMaxParallel"), QStringLiteral("agent"),
         QT_TRANSLATE_NOOP("SettingsSearch", "子代理并行上限"),
         QStringLiteral("子代理 subagent 并行 并发 上限 parallel"), false, kPage},
        {NEXUS_CATALOG_ID("agent/subagentMaxSteps"), QStringLiteral("agent"),
         QT_TRANSLATE_NOOP("SettingsSearch", "子代理步数预算"),
         QStringLiteral("子代理 subagent 步数 预算 steps"), false, kPage},
        {NEXUS_CATALOG_ID("agent/subagentTimeoutSec"), QStringLiteral("agent"),
         QT_TRANSLATE_NOOP("SettingsSearch", "子代理超时（秒）"),
         QStringLiteral("子代理 subagent 超时 timeout 秒"), false, kPage},

        // ── 工具与能力 ──────────────────────────────────────
        {NEXUS_CATALOG_ID("tools/disabled"), QStringLiteral("tools"),
         QT_TRANSLATE_NOOP("SettingsSearch", "工具管理"),
         QStringLiteral("工具 禁用 启用 shell read_file write_file 需确认 tools"), false, kNone},
        {NEXUS_CATALOG_ID("webSearch/provider"), QStringLiteral("tools"),
         QT_TRANSLATE_NOOP("SettingsSearch", "联网搜索服务"),
         QStringLiteral("联网 搜索 web_search duckduckgo zhipu deepseek 智谱 提供方 provider"),
         false, kPage},
        {NEXUS_CATALOG_ID("webSearch/apiKey"), QStringLiteral("tools"),
         QT_TRANSLATE_NOOP("SettingsSearch", "联网搜索 API Key"),
         QStringLiteral("联网 搜索 密钥 api key zhipu 智谱 deepseek"), false, kNone},
        {NEXUS_CATALOG_ID("webSearch/zhipuApiKey"), QStringLiteral("tools"),
         QT_TRANSLATE_NOOP("SettingsSearch", "智谱搜索密钥"),
         QStringLiteral("zhipu 智谱 webSearch apiKey"), true, kNone},
        {NEXUS_CATALOG_ID("webSearch/deepseekApiKey"), QStringLiteral("tools"),
         QT_TRANSLATE_NOOP("SettingsSearch", "DeepSeek 搜索密钥"),
         QStringLiteral("deepseek webSearch apiKey"), true, kNone},
        {NEXUS_CATALOG_ID("agent/defaultPermissionMode"), QStringLiteral("tools"),
         QT_TRANSLATE_NOOP("SettingsSearch", "默认权限模式"),
         QStringLiteral("权限 模式 确认 自动编辑 完全访问 只读 计划 permission plan read only plan_only"),
         false, kPage},

        // ── MCP ────────────────────────────────────────────
        {NEXUS_CATALOG_ID("mcp/servers"), QStringLiteral("mcp"),
         QT_TRANSLATE_NOOP("SettingsSearch", "MCP 服务器"),
         QStringLiteral("mcp 服务器 添加 命令 url headers 环境变量 连接 重连 启用"), false, kNone},

        // ── 网络与代理 ──────────────────────────────────────
        {NEXUS_CATALOG_ID("network/proxyMode"), QStringLiteral("network"),
         QT_TRANSLATE_NOOP("SettingsSearch", "代理模式"),
         QStringLiteral("代理 proxy http https 网络 模式 system manual off"), false, kPage},
        {NEXUS_CATALOG_ID("network/proxyHost"), QStringLiteral("network"),
         QT_TRANSLATE_NOOP("SettingsSearch", "代理服务器地址"),
         QStringLiteral("代理 proxy 主机 host 地址"), false, kPage},
        {NEXUS_CATALOG_ID("network/proxyPort"), QStringLiteral("network"),
         QT_TRANSLATE_NOOP("SettingsSearch", "代理端口"),
         QStringLiteral("代理 proxy 端口 port"), false, kPage},
        {NEXUS_CATALOG_ID("network/proxyUsername"), QStringLiteral("network"),
         QT_TRANSLATE_NOOP("SettingsSearch", "代理用户名"),
         QStringLiteral("代理 proxy 认证 auth 用户名 username"), false, kPage},
        {NEXUS_CATALOG_ID("network/proxyPassword"), QStringLiteral("network"),
         QT_TRANSLATE_NOOP("SettingsSearch", "代理密码"),
         QStringLiteral("代理 proxy 认证 auth 密码 password"), false, kPage},
        {NEXUS_CATALOG_ID("network/proxyAuth"), QStringLiteral("network"),
         QT_TRANSLATE_NOOP("SettingsSearch", "代理认证数据"),
         QStringLiteral("proxyAuth"), true, kPage},
        {NEXUS_CATALOG_ID("network/testing"), QStringLiteral("network"),
         QT_TRANSLATE_NOOP("SettingsSearch", "测试代理"),
         QStringLiteral("测试 连接 代理 test proxy"), false, kNone},

        // ── 安全与隐私 ──────────────────────────────────────
        {NEXUS_CATALOG_ID("security/allowFullDiskRead"), QStringLiteral("security"),
         QT_TRANSLATE_NOOP("SettingsSearch", "允许读取整个磁盘"),
         QStringLiteral("读取 沙箱 全盘 磁盘 read 策略"), false, kPage},
        {NEXUS_CATALOG_ID("security/allowProjectWrite"), QStringLiteral("security"),
         QT_TRANSLATE_NOOP("SettingsSearch", "允许写入当前项目目录"),
         QStringLiteral("写入 项目 write project"), false, kPage},
        {NEXUS_CATALOG_ID("security/readDenyList"), QStringLiteral("security"),
         QT_TRANSLATE_NOOP("SettingsSearch", "黑名单"),
         QStringLiteral("黑名单 拒绝 deny 敏感 追加 读取"), false, kPage},
        {NEXUS_CATALOG_ID("security/localOnlyMode"), QStringLiteral("security"),
         QT_TRANSLATE_NOOP("SettingsSearch", "仅本地模式"),
         QStringLiteral("仅本地 本地模式 local 隐私 offline"), false, kPage},
        {NEXUS_CATALOG_ID("security/allowLocalNetworkFetch"), QStringLiteral("security"),
         QT_TRANSLATE_NOOP("SettingsSearch", "允许 http_fetch 访问本机与内网地址"),
         QStringLiteral("http_fetch 本机 内网 ssrf 网络 fetch"), false, kPage},

        // ── 记忆管理 ────────────────────────────────────────
        {NEXUS_CATALOG_ID("memory/episodicMemoryEnabled"), QStringLiteral("memory"),
         QT_TRANSLATE_NOOP("SettingsSearch", "启用长期记忆"),
         QStringLiteral("记忆 episodic 摘要 长期 检索 memory"), false, kPage},
        {NEXUS_CATALOG_ID("memory/auditRetentionDays"), QStringLiteral("memory"),
         QT_TRANSLATE_NOOP("SettingsSearch", "审计日志保留天数"),
         QStringLiteral("保留 审计 清理 天数 retention audit"), false, kPage},
        {NEXUS_CATALOG_ID("memory/tokenRetentionDays"), QStringLiteral("memory"),
         QT_TRANSLATE_NOOP("SettingsSearch", "Token 用量保留天数"),
         QStringLiteral("保留 token 用量 天数 retention"), false, kPage},
        {NEXUS_CATALOG_ID("memory/episodes"), QStringLiteral("memory"),
         QT_TRANSLATE_NOOP("SettingsSearch", "长期记忆列表"),
         QStringLiteral("记忆 浏览 搜索 遗忘 清空 episodic"), false, kNone},
        {NEXUS_CATALOG_ID("memory/backupOnStartup"), QStringLiteral("backup"),
         QT_TRANSLATE_NOOP("SettingsSearch", "启动时自动备份"),
         QStringLiteral("备份 backup startup 自动"), false, kPage},
        {NEXUS_CATALOG_ID("memory/backupKeepCount"), QStringLiteral("backup"),
         QT_TRANSLATE_NOOP("SettingsSearch", "备份保留数量"),
         QStringLiteral("备份 backup keep 保留 数量"), false, kPage},
        {NEXUS_CATALOG_ID("memory/backupIntervalHours"), QStringLiteral("backup"),
         QT_TRANSLATE_NOOP("SettingsSearch", "定时备份间隔"),
         QStringLiteral("备份 backup 定时 间隔 周期 interval schedule"), false, kPage},

        // ── 知识库嵌入 ──────────────────────────────────────
        {NEXUS_CATALOG_ID("embedding/provider"), QStringLiteral("knowledge"),
         QT_TRANSLATE_NOOP("SettingsSearch", "嵌入服务提供方"),
         QStringLiteral("嵌入 embedding ollama 向量 提供方 provider openai 知识库"), false, kPage},
        {NEXUS_CATALOG_ID("embedding/model"), QStringLiteral("knowledge"),
         QT_TRANSLATE_NOOP("SettingsSearch", "嵌入模型"),
         QStringLiteral("嵌入 embedding 模型 model 向量"), false, kPage},
        {NEXUS_CATALOG_ID("embedding/baseUrl"), QStringLiteral("knowledge"),
         QT_TRANSLATE_NOOP("SettingsSearch", "嵌入服务地址"),
         QStringLiteral("嵌入 embedding 服务地址 baseUrl 端点 知识库"), false, kPage},
        {NEXUS_CATALOG_ID("embedding/apiKey"), QStringLiteral("knowledge"),
         QT_TRANSLATE_NOOP("SettingsSearch", "向量模型 API Key"),
         QStringLiteral("嵌入 embedding 密钥 apiKey key openai"), false, kPage},

        // ── 提示词与指令 ────────────────────────────────────
        {NEXUS_CATALOG_ID("prompts/presets"), QStringLiteral("prompts"),
         QT_TRANSLATE_NOOP("SettingsSearch", "系统提示预设"),
         QStringLiteral("预设 prompt 预设库 角色 系统 提示词"), false, kNone},
        {NEXUS_CATALOG_ID("prompts/slash"), QStringLiteral("prompts"),
         QT_TRANSLATE_NOOP("SettingsSearch", "斜杠指令"),
         QStringLiteral("斜杠 slash 指令 command 快捷 展开"), false, kNone},
        {NEXUS_CATALOG_ID("prompts/templates"), QStringLiteral("prompts"),
         QT_TRANSLATE_NOOP("SettingsSearch", "会话模板"),
         QStringLiteral("模板 template 新会话 预置 首条消息"), false, kNone},
        {NEXUS_CATALOG_ID("promptPresets"), QStringLiteral("prompts"),
         QT_TRANSLATE_NOOP("SettingsSearch", "提示词预设数据"),
         QStringLiteral("promptPresets"), true, kNone},
        {NEXUS_CATALOG_ID("slashCommands"), QStringLiteral("prompts"),
         QT_TRANSLATE_NOOP("SettingsSearch", "斜杠指令数据"),
         QStringLiteral("slashCommands"), true, kNone},
        {NEXUS_CATALOG_ID("sessionTemplates"), QStringLiteral("prompts"),
         QT_TRANSLATE_NOOP("SettingsSearch", "会话模板数据"),
         QStringLiteral("sessionTemplates"), true, kNone},

        // ── 项目规则 ────────────────────────────────────────
        {NEXUS_CATALOG_ID("projects/rules"), QStringLiteral("projects"),
         QT_TRANSLATE_NOOP("SettingsSearch", "项目规则"),
         QStringLiteral("AGENTS agents 规则 约定 项目 rules 注入 模板 nexus"), false, kNone},
        {NEXUS_CATALOG_ID("projects/activeId"), QStringLiteral("projects"),
         QT_TRANSLATE_NOOP("SettingsSearch", "当前项目"),
         QStringLiteral("activeId project"), true, kNone},

        // ── 用量与配额 ──────────────────────────────────────
        {NEXUS_CATALOG_ID("dailyTokenQuota"), QStringLiteral("usage"),
         QT_TRANSLATE_NOOP("SettingsSearch", "每日 Token 上限"),
         QStringLiteral("配额 token 每日 上限 quota daily"), false, kPage},
        {NEXUS_CATALOG_ID("monthlyTokenQuota"), QStringLiteral("usage"),
         QT_TRANSLATE_NOOP("SettingsSearch", "每月 Token 上限"),
         QStringLiteral("配额 token 每月 上限 quota monthly"), false, kPage},
        {NEXUS_CATALOG_ID("dailyCostQuotaCents"), QStringLiteral("usage"),
         QT_TRANSLATE_NOOP("SettingsSearch", "每日费用上限"),
         QStringLiteral("配额 费用 成本 元 每日 daily cost"), false, kPage},
        {NEXUS_CATALOG_ID("monthlyCostQuotaCents"), QStringLiteral("usage"),
         QT_TRANSLATE_NOOP("SettingsSearch", "每月费用上限"),
         QStringLiteral("配额 费用 成本 元 每月 monthly cost"), false, kPage},
        {NEXUS_CATALOG_ID("strictQuotaMode"), QStringLiteral("usage"),
         QT_TRANSLATE_NOOP("SettingsSearch", "超限拒绝请求"),
         QStringLiteral("配额 超限 拒绝 strict quota"), false, kPage},
        {NEXUS_CATALOG_ID("usage/alertPercent"), QStringLiteral("usage"),
         QT_TRANSLATE_NOOP("SettingsSearch", "用量告警阈值"),
         QStringLiteral("配额 告警 提醒 阈值 百分比 alert percent usage"), false, kPage},
        {NEXUS_CATALOG_ID("usage/overview"), QStringLiteral("usage"),
         QT_TRANSLATE_NOOP("SettingsSearch", "用量统计"),
         QStringLiteral("用量 统计 消耗 成本 成本明细 usage token"), false, kNone},

        // ── 活动记录 ────────────────────────────────────────
        {NEXUS_CATALOG_ID("activity/log"), QStringLiteral("activity"),
         QT_TRANSLATE_NOOP("SettingsSearch", "活动记录"),
         QStringLiteral("活动 轨迹 usage 操作 记录 audit trail 时间线"), false, kNone},

        // ── 日志与诊断 ──────────────────────────────────────
        {NEXUS_CATALOG_ID("logging/level"), QStringLiteral("logs"),
         QT_TRANSLATE_NOOP("SettingsSearch", "日志级别"),
         QStringLiteral("日志 级别 log level debug info warning critical 诊断"), false, kPage},
        {NEXUS_CATALOG_ID("logging/directory"), QStringLiteral("logs"),
         QT_TRANSLATE_NOOP("SettingsSearch", "日志目录"),
         QStringLiteral("日志 目录 保存路径 directory folder"), false, kPage},
        {NEXUS_CATALOG_ID("logging/maxFileSizeMb"), QStringLiteral("logs"),
         QT_TRANSLATE_NOOP("SettingsSearch", "单个日志文件上限"),
         QStringLiteral("日志 大小 上限 轮转 文件 maxFileSize"), false, kPage},
        {NEXUS_CATALOG_ID("logging/retentionFiles"), QStringLiteral("logs"),
         QT_TRANSLATE_NOOP("SettingsSearch", "保留日志文件数"),
         QStringLiteral("日志 保留 数量 轮转 retention"), false, kPage},

        // ── 备份与恢复 ──────────────────────────────────────
        {NEXUS_CATALOG_ID("backup/settings"), QStringLiteral("backup"),
         QT_TRANSLATE_NOOP("SettingsSearch", "设置导出与导入"),
         QStringLiteral("导出 导入 备份 json 设置"), false, kNone},
        {NEXUS_CATALOG_ID("backup/database"), QStringLiteral("backup"),
         QT_TRANSLATE_NOOP("SettingsSearch", "数据库备份"),
         QStringLiteral("数据库 备份 恢复 快照 sqlite 预览"), false, kNone},

        // ── 运行时/内部键（登记但不出现在搜索结果）──────────
        {NEXUS_CATALOG_ID("ui/sidebarWidth"), QStringLiteral("interface"),
         QT_TRANSLATE_NOOP("SettingsSearch", "侧边栏宽度"), QStringLiteral("ui sidebarWidth"),
         true, kNone},
        {NEXUS_CATALOG_ID("ui/panelWidth"), QStringLiteral("interface"),
         QT_TRANSLATE_NOOP("SettingsSearch", "面板宽度"), QStringLiteral("ui panelWidth"),
         true, kNone},
        {NEXUS_CATALOG_ID("ui/panelVisible"), QStringLiteral("interface"),
         QT_TRANSLATE_NOOP("SettingsSearch", "面板可见"), QStringLiteral("ui panelVisible"),
         true, kNone},
        {NEXUS_CATALOG_ID("ui/sidebarExpanded"), QStringLiteral("interface"),
         QT_TRANSLATE_NOOP("SettingsSearch", "侧边栏展开"), QStringLiteral("ui sidebarExpanded"),
         true, kNone},
        {NEXUS_CATALOG_ID("ui/showProcess"), QStringLiteral("interface"),
         QT_TRANSLATE_NOOP("SettingsSearch", "显示过程"), QStringLiteral("ui showProcess"),
         true, kNone},
        {NEXUS_CATALOG_ID("ui/windowX"), QStringLiteral("interface"),
         QT_TRANSLATE_NOOP("SettingsSearch", "窗口位置"), QStringLiteral("window x"), true, kNone},
        {NEXUS_CATALOG_ID("ui/windowY"), QStringLiteral("interface"),
         QT_TRANSLATE_NOOP("SettingsSearch", "窗口位置"), QStringLiteral("window y"), true, kNone},
        {NEXUS_CATALOG_ID("ui/windowWidth"), QStringLiteral("interface"),
         QT_TRANSLATE_NOOP("SettingsSearch", "窗口宽度"), QStringLiteral("window width"), true, kNone},
        {NEXUS_CATALOG_ID("ui/windowHeight"), QStringLiteral("interface"),
         QT_TRANSLATE_NOOP("SettingsSearch", "窗口高度"), QStringLiteral("window height"), true, kNone},
        {NEXUS_CATALOG_ID("ui/windowMaximized"), QStringLiteral("interface"),
         QT_TRANSLATE_NOOP("SettingsSearch", "窗口最大化"), QStringLiteral("window maximized"),
         true, kNone},
        {NEXUS_CATALOG_ID("schemaVersion"), QStringLiteral("backup"),
         QT_TRANSLATE_NOOP("SettingsSearch", "设置版本"), QStringLiteral("schemaVersion"),
         true, kNone},
        {NEXUS_CATALOG_ID("theme/dark"), QStringLiteral("interface"),
         QT_TRANSLATE_NOOP("SettingsSearch", "旧版深色主题"), QStringLiteral("theme dark legacy"),
         true, kNone},
    };
    return values;
}

struct ResolvedEntry {
    QString id;
    QString categoryId;
    QString title;
    QString keywords;
    CatalogResetScope reset{CatalogResetScope::None};
    bool hidden{false};
};

QString translateSearch(const QString& source) {
    return QCoreApplication::translate("SettingsSearch", source.toUtf8().constData());
}

ResolvedEntry resolve(const CatalogEntryDef& def) {
    return {def.id, def.categoryId, translateSearch(def.title), def.keywords,
            def.reset, def.hidden};
}

ResolvedEntry resolvePreference(const PreferenceMeta& meta) {
    ResolvedEntry entry;
    entry.id = meta.key;
    entry.categoryId = meta.categoryId;
    entry.title = QCoreApplication::translate("NexusAI", meta.displayName.toUtf8().constData());
    entry.keywords = meta.key + QLatin1Char(' ') + meta.legacyKeys.join(QLatin1Char(' '));
    entry.hidden = meta.categoryId.isEmpty();
    entry.reset = entry.hidden ? kNone : kPref;
    return entry;
}

QVariantMap toMap(const ResolvedEntry& entry) {
    PreferenceRegistry* registry = PreferenceRegistry::instance();
    QVariantMap map;
    map.insert(QStringLiteral("id"), entry.id);
    map.insert(QStringLiteral("anchor"), entry.id);
    map.insert(QStringLiteral("title"), entry.title);
    map.insert(QStringLiteral("category"), entry.categoryId);
    map.insert(QStringLiteral("categoryName"), registry->categoryName(entry.categoryId));
    map.insert(QStringLiteral("keywords"), entry.keywords);
    map.insert(QStringLiteral("hidden"), entry.hidden);
    map.insert(QStringLiteral("resetMode"),
               entry.reset == CatalogResetScope::Preference ? QStringLiteral("preference")
               : entry.reset == CatalogResetScope::Page ? QStringLiteral("page")
                                                        : QStringLiteral("none"));
    return map;
}

// 值快照只需要"分类 + 复位来源 + 是否隐藏"这三项事实，不依赖翻译。
// 静态表 + schema 在进程内不变，建一次缓存即可——否则每行每次 revision
// 变化都要重走 74 条 allEntries()。
struct EntryFacts {
    QString categoryId;
    CatalogResetScope reset{CatalogResetScope::None};
    bool hidden{false};
};

const QHash<QString, EntryFacts>& entryFacts() {
    static const QHash<QString, EntryFacts> facts = [] {
        QHash<QString, EntryFacts> map;
        for (const CatalogEntryDef& def : staticEntries())
            map.insert(def.id, {def.categoryId, def.reset, def.hidden});
        for (const PreferenceMeta& meta : SettingsSchema::preferences()) {
            const bool docked = !meta.categoryId.isEmpty();
            map.insert(meta.key,
                       {meta.categoryId, docked ? kPref : kNone, !docked});
        }
        return map;
    }();
    return facts;
}

struct SearchHit {
    ResolvedEntry entry;
    int score{0};
    QString categoryName;
};

// 命中权重：标题 > 关键词 > 键名/id > 分类名。同分再按分类顺序与标题排，
// 保证同一查询两次结果顺序稳定。
int searchScore(const ResolvedEntry& entry, const QString& needle,
                const QString& categoryName) {
    if (entry.title.toLower().contains(needle)) return 100;
    if (entry.keywords.toLower().contains(needle)) return 60;
    if (entry.id.toLower().contains(needle)) return 40;
    if (categoryName.toLower().contains(needle)) return 20;
    return 0;
}

// 标题命中区间 + 富文本（<b>）。先在转义后的标题上定位：& < > 转义会改变
// 长度，索引必须与实际渲染的字符串对齐。返回的 highlightStart/Length 也
// 基于转义串，QML 只消费 titleHtml，两者只做诊断用。
QString highlightedTitle(const QString& title, const QString& needle,
                         int* highlightStart, int* highlightLength) {
    *highlightStart = -1;
    *highlightLength = 0;
    const QString escaped = title.toHtmlEscaped();
    const int index = escaped.toLower().indexOf(needle);
    if (index < 0 || needle.isEmpty()) return escaped;
    *highlightStart = index;
    *highlightLength = needle.length();
    return escaped.left(index) + QStringLiteral("<b>")
        + escaped.mid(index, needle.length()) + QStringLiteral("</b>")
        + escaped.mid(index + needle.length());
}

QVector<ResolvedEntry> allEntries() {
    QVector<ResolvedEntry> out;
    for (const PreferenceMeta& meta : SettingsSchema::preferences())
        out.append(resolvePreference(meta));
    for (const CatalogEntryDef& def : staticEntries())
        out.append(resolve(def));
    return out;
}

} // namespace

SettingsCatalog* SettingsCatalog::instance() {
    static SettingsCatalog* catalog = new SettingsCatalog(nullptr);
    return catalog;
}

SettingsCatalog* SettingsCatalog::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

SettingsCatalog::SettingsCatalog(QObject* parent)
    : QObject(parent) {
    // 值快照与"已修改"徽标的失效源。两条写入通道都要接：偏好经
    // PreferenceStore（含 EditBuffer::stage 的写穿），M1 键经 SettingsService。
    connect(PreferenceStore::instance(), &PreferenceStore::saved, this,
            [this](const QStringList&) { bumpRevision(); });
    connect(SettingsService::instance(), &SettingsService::settingSaved, this,
            [this](const QString&) { bumpRevision(); });
}

void SettingsCatalog::bumpRevision() {
    ++m_revision;
    emit revisionChanged();
}

QVariantMap SettingsCatalog::describe(const QString& id) const {
    QVariantMap map;
    map.insert(QStringLiteral("id"), id);

    const SettingValueSnapshot snapshot = SettingsValue::inspect(id);
    map.insert(QStringLiteral("known"), snapshot.known);
    map.insert(QStringLiteral("secret"), snapshot.secret);
    map.insert(QStringLiteral("configured"), snapshot.configured);
    map.insert(QStringLiteral("modified"), snapshot.modified);
    map.insert(QStringLiteral("display"), SettingsValue::displayOf(snapshot, id));

    const auto facts = entryFacts().constFind(id);
    const bool registered = facts != entryFacts().cend();
    map.insert(QStringLiteral("category"),
               registered ? facts->categoryId : QString());
    // 可复原 = 有登记 + 有复位通道 + 非密钥 + 默认值有效。默认值无效的
    // 情况真实存在：ui.fontSans 的默认值要等 FontProbe 探测后才填。
    map.insert(QStringLiteral("revertable"),
               registered && snapshot.known && !snapshot.secret
                   && facts->reset != kNone && snapshot.defaultValue.isValid());
    return map;
}

bool SettingsCatalog::isModified(const QString& id) const {
    return SettingsValue::inspect(id).modified;
}

int SettingsCatalog::modifiedCount(const QString& categoryId) const {
    if (m_modifiedCountRevision != m_revision) rebuildModifiedCounts();
    return m_modifiedCounts.value(categoryId);
}

void SettingsCatalog::rebuildModifiedCounts() const {
    m_modifiedCounts.clear();
    const auto& facts = entryFacts();
    for (auto it = facts.cbegin(); it != facts.cend(); ++it) {
        // 隐藏条目（导航自身状态、profile 数据）与没有复位通道的条目
        // 不计入：徽标只告诉用户"这一页有可复原的改动"。
        if (it->hidden || it->reset == kNone) continue;
        if (SettingsValue::inspect(it.key()).modified)
            ++m_modifiedCounts[it->categoryId];
    }
    m_modifiedCountRevision = m_revision;
}

bool SettingsCatalog::resetValue(const QString& id) {
    const auto& facts = entryFacts();
    const auto it = facts.constFind(id);
    if (it == facts.cend() || it->reset == kNone) return false;

    const SettingValueSnapshot snapshot = SettingsValue::inspect(id);
    // 密钥与凭据不参与复位（docs/31 §6 红线 1）；没有有效默认值时不写回。
    if (!snapshot.known || snapshot.secret || !snapshot.defaultValue.isValid())
        return false;

    bool ok = false;
    if (it->reset == kPref) {
        EditBuffer* buffer = EditBuffer::instance();
        buffer->stage(id, snapshot.defaultValue);
        // stage 是写穿语义：成功时立即落盘且不留 staged；校验失败或写盘
        // 失败会把值留在 staged 里等用户修正。残留即视为未复位，不能
        // 对调用方报成功（否则底栏会显示"已恢复"而值没变）。
        ok = !buffer->stagedKeys().contains(id);
    } else {
        ok = SettingsService::instance()->setValueForSetting(id,
                                                             snapshot.defaultValue);
    }
    if (ok) bumpRevision();
    return ok;
}

QVariantList SettingsCatalog::entries() const {
    PreferenceRegistry* registry = PreferenceRegistry::instance();
    QVector<ResolvedEntry> visible;
    for (const ResolvedEntry& entry : allEntries())
        if (!entry.hidden && !entry.categoryId.isEmpty())
            visible.append(entry);
    std::sort(visible.begin(), visible.end(), [registry](const ResolvedEntry& a,
                                                        const ResolvedEntry& b) {
        const int orderA = registry->categoryOrder(a.categoryId);
        const int orderB = registry->categoryOrder(b.categoryId);
        if (orderA != orderB) return orderA < orderB;
        return QString::compare(a.title, b.title, Qt::CaseInsensitive) < 0;
    });
    QVariantList result;
    result.reserve(visible.size());
    for (const ResolvedEntry& entry : visible)
        result.append(toMap(entry));
    return result;
}

QVariantList SettingsCatalog::search(const QString& query) const {
    const QString needle = query.trimmed().toLower();
    if (needle.isEmpty()) return {};

    PreferenceRegistry* registry = PreferenceRegistry::instance();
    QVector<SearchHit> hits;
    for (const ResolvedEntry& entry : allEntries()) {
        if (entry.hidden || entry.categoryId.isEmpty()) continue;
        const QString categoryName = registry->categoryName(entry.categoryId);
        const int score = searchScore(entry, needle, categoryName);
        if (score <= 0) continue;
        hits.append({entry, score, categoryName});
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [registry](const SearchHit& a, const SearchHit& b) {
        if (a.score != b.score) return a.score > b.score;
        const int orderA = registry->categoryOrder(a.entry.categoryId);
        const int orderB = registry->categoryOrder(b.entry.categoryId);
        if (orderA != orderB) return orderA < orderB;
        return QString::compare(a.entry.title, b.entry.title,
                                Qt::CaseInsensitive) < 0;
    });

    QVariantList results;
    results.reserve(hits.size());
    for (const SearchHit& hit : hits) {
        QVariantMap map = toMap(hit.entry);
        map.insert(QStringLiteral("categoryName"), hit.categoryName);
        int start = -1;
        int length = 0;
        map.insert(QStringLiteral("titleHtml"),
                   highlightedTitle(hit.entry.title, needle, &start, &length));
        map.insert(QStringLiteral("highlightStart"), start);
        map.insert(QStringLiteral("highlightLength"), length);
        map.insert(QStringLiteral("score"), hit.score);
        // 当前值摘要：密钥类只会得到"已配置/未配置"，不会带出任何值。
        const SettingValueSnapshot snapshot = SettingsValue::inspect(hit.entry.id);
        map.insert(QStringLiteral("currentDisplay"),
                   SettingsValue::displayOf(snapshot, hit.entry.id));
        map.insert(QStringLiteral("modified"), snapshot.modified);
        map.insert(QStringLiteral("secret"), snapshot.secret);
        results.append(map);
    }
    return results;
}

QVariantMap SettingsCatalog::entry(const QString& id) const {
    if (id.isEmpty()) return {};
    for (const ResolvedEntry& entry : allEntries())
        if (entry.id == id) return toMap(entry);
    return {};
}

bool SettingsCatalog::hasEntry(const QString& id) const {
    return !entry(id).isEmpty();
}

QString SettingsCatalog::resetMode(const QString& categoryId) const {
    bool hasPreference = false;
    bool hasPage = false;
    for (const ResolvedEntry& entry : allEntries()) {
        if (entry.categoryId != categoryId) continue;
        if (entry.reset == CatalogResetScope::Page) hasPage = true;
        else if (entry.reset == CatalogResetScope::Preference) hasPreference = true;
    }
    if (hasPage) return QStringLiteral("page");
    if (hasPreference) return QStringLiteral("preference");
    return QStringLiteral("none");
}

} // namespace NexusAI
