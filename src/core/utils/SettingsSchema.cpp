#include "SettingsSchema.h"

#include "core/agent/AIAgent.h"
#include "core/utils/SettingsRepository.h"
#include "core/tools/WebSearchBackend.h"

#include <QMetaType>
#include <cmath>
#include <climits>
#include <limits>

namespace NexusAI {
namespace {

PreferenceConstraint integerRange(int min, int max) {
    PreferenceConstraint constraint;
    constraint.min = min;
    constraint.max = max;
    constraint.hasRange = true;
    return constraint;
}

PreferenceConstraint allowedValues(const QStringList& values) {
    PreferenceConstraint constraint;
    constraint.options = values;
    return constraint;
}

PreferenceConstraint requiredText() {
    PreferenceConstraint constraint;
    constraint.required = true;
    return constraint;
}

SettingsKeyDef keyDef(const QString& key, const QVariant& defaultValue,
                      SettingValueType type, const PreferenceConstraint& constraint = {},
                      bool encrypted = false, bool legacy = false) {
    return {key, defaultValue, type, constraint, encrypted, legacy};
}

SettingValueType valueType(PreferenceType type) {
    switch (type) {
    case PreferenceType::Bool: return SettingValueType::Bool;
    case PreferenceType::Int: return SettingValueType::Int;
    case PreferenceType::Enum:
    case PreferenceType::String: return SettingValueType::String;
    }
    return SettingValueType::Any;
}

PreferenceMeta preference(const QString& key, const QString& category,
                          const QString& displayName, const QVariant& defaultValue,
                          PreferenceType type,
                          const PreferenceConstraint& constraint = {},
                          const QStringList& legacyKeys = {}) {
    return {key, category, displayName, defaultValue, type, constraint, legacyKeys};
}

} // namespace

const QVector<CategoryGroupMeta>& SettingsSchema::categoryGroups() {
    static const QVector<CategoryGroupMeta> values = {
        {QStringLiteral("ai"), QT_TRANSLATE_NOOP("NexusAI", "模型与对话"), 1},
        {QStringLiteral("appearance"), QT_TRANSLATE_NOOP("NexusAI", "外观与常规"), 2},
        {QStringLiteral("content"), QT_TRANSLATE_NOOP("NexusAI", "知识与内容"), 3},
        {QStringLiteral("data"), QT_TRANSLATE_NOOP("NexusAI", "数据与诊断"), 4},
    };
    return values;
}

const QVector<CategoryMeta>& SettingsSchema::categories() {
    static const QVector<CategoryMeta> values = {
        // ── 模型与对话 ───────────────────────────────────
        {QStringLiteral("model"), QT_TRANSLATE_NOOP("NexusAI", "模型配置"), 1,
         QStringLiteral("ai")},
        // 智能体行为：原「通用设置」的模型行为区块（步数预算 / 任务超时 /
        // 自定义系统提示词）。与模型配置分开：前者是"用什么模型"，后者是
        // "智能体怎么跑"。
        {QStringLiteral("agent"), QT_TRANSLATE_NOOP("NexusAI", "智能体行为"), 2,
         QStringLiteral("ai")},
        {QStringLiteral("tools"), QT_TRANSLATE_NOOP("NexusAI", "工具与能力"), 3,
         QStringLiteral("ai")},
        {QStringLiteral("mcp"), QT_TRANSLATE_NOOP("NexusAI", "MCP 服务器"), 4,
         QStringLiteral("ai")},
        // ── 外观与常规 ───────────────────────────────────
        {QStringLiteral("interface"), QT_TRANSLATE_NOOP("NexusAI", "界面与外观"), 5,
         QStringLiteral("appearance")},
        // 通用页收窄为真正的"应用常规"：托盘 / 自启 / 通知。
        {QStringLiteral("general"), QT_TRANSLATE_NOOP("NexusAI", "常规应用"), 6,
         QStringLiteral("appearance")},
        // 网络代理从通用页拆出：它作用于模型 / 向量 / 搜索 / MCP 四条链路，
        // 是跨模块的基础设施，不应藏在"常规"里。
        {QStringLiteral("network"), QT_TRANSLATE_NOOP("NexusAI", "网络与代理"), 7,
         QStringLiteral("appearance")},
        {QStringLiteral("security"), QT_TRANSLATE_NOOP("NexusAI", "安全与隐私"), 8,
         QStringLiteral("appearance")},
        // ── 知识与内容 ───────────────────────────────────
        {QStringLiteral("memory"), QT_TRANSLATE_NOOP("NexusAI", "记忆管理"), 9,
         QStringLiteral("content")},
        // 知识库嵌入独立成页：记忆页主体是摘要列表，设置项挤在列表上方会
        // 持续压缩浏览区；嵌入本质是"知识库使用哪个向量服务"。
        {QStringLiteral("knowledge"), QT_TRANSLATE_NOOP("NexusAI", "知识库嵌入"), 10,
         QStringLiteral("content")},
        {QStringLiteral("prompts"), QT_TRANSLATE_NOOP("NexusAI", "提示词与指令"), 11,
         QStringLiteral("content")},
        {QStringLiteral("projects"), QT_TRANSLATE_NOOP("NexusAI", "项目规则"), 12,
         QStringLiteral("content")},
        // ── 数据与诊断 ───────────────────────────────────
        {QStringLiteral("usage"), QT_TRANSLATE_NOOP("NexusAI", "用量与配额"), 13,
         QStringLiteral("data")},
        {QStringLiteral("activity"), QT_TRANSLATE_NOOP("NexusAI", "活动记录"), 14,
         QStringLiteral("data")},
        {QStringLiteral("logs"), QT_TRANSLATE_NOOP("NexusAI", "日志与诊断"), 15,
         QStringLiteral("data")},
        {QStringLiteral("backup"), QT_TRANSLATE_NOOP("NexusAI", "备份与恢复"), 16,
         QStringLiteral("data")},
        // docs/31 N2：全局快捷键。动作表在 ShortcutMap，偏好只存覆盖项；
        // 归属“外观与常规”，因为快捷键属于应用习惯而非模型行为。
        {QStringLiteral("shortcuts"), QT_TRANSLATE_NOOP("NexusAI", "快捷键"), 17,
         QStringLiteral("appearance")},
        // docs/31 S4.5 N5：数据管理总览。与用量/活动/日志/备份同属数据组，
        // 但主体是"统计 + 立即清理/清空/VACUUM"的动作面板而非偏好键，
        // 独立成页避免继续堆高备份页。放在数据组末尾。
        {QStringLiteral("storage"), QT_TRANSLATE_NOOP("NexusAI", "数据管理"), 18,
         QStringLiteral("data")},
        // docs/37 W3：技能是独立能力（发现/引用/注入），既不是工具也不是
        // 记忆，因此单独成页；排内容组末尾，不打乱既有分类顺序。
        {QStringLiteral("skills"), QT_TRANSLATE_NOOP("NexusAI", "技能"), 19,
         QStringLiteral("content")},
    };
    return values;
}

const QVector<PreferenceMeta>& SettingsSchema::preferences() {
    static const QVector<PreferenceMeta> values = {
        preference(QStringLiteral("settings.lastCategory"), QString(),
                   QT_TRANSLATE_NOOP("NexusAI", "上次访问分类"), QStringLiteral("model"),
                   PreferenceType::String),
        // 设置中心左侧分组折叠状态（逗号分隔的 groupId）。与 lastCategory
        // 同属「导航自身的写入」：无分类、无显示名、不进搜索与备份。
        preference(QStringLiteral("settings.collapsedGroups"), QString(),
                   QT_TRANSLATE_NOOP("NexusAI", "导航分组折叠状态"), QString(),
                   PreferenceType::String),
        preference(QStringLiteral("ui.themeMode"), QStringLiteral("interface"),
                   QT_TRANSLATE_NOOP("NexusAI", "主题模式"), QStringLiteral("auto"),
                   PreferenceType::Enum,
                   allowedValues({QStringLiteral("auto"), QStringLiteral("dark"),
                                  QStringLiteral("light")}),
                   {QStringLiteral("theme/dark")}),
        // Font family defaults are filled from FontProbe during ThemeManager startup.
        preference(QStringLiteral("ui.fontSans"), QStringLiteral("interface"),
                   QT_TRANSLATE_NOOP("NexusAI", "正文字体"), QVariant(),
                   PreferenceType::String),
        preference(QStringLiteral("ui.fontMono"), QStringLiteral("interface"),
                   QT_TRANSLATE_NOOP("NexusAI", "等宽字体"), QVariant(),
                   PreferenceType::String),
        preference(QStringLiteral("ui.baseFontSize"), QStringLiteral("interface"),
                   QT_TRANSLATE_NOOP("NexusAI", "基础字号"), 14,
                   PreferenceType::Int, integerRange(10, 24)),
        preference(QStringLiteral("ui.locale"), QStringLiteral("interface"),
                   QT_TRANSLATE_NOOP("NexusAI", "界面语言"), QString(),
                   PreferenceType::Enum,
                   allowedValues({QStringLiteral("auto"), QStringLiteral("zh-CN"),
                                  QStringLiteral("en")})),
        preference(QStringLiteral("ui.defaultSidebarWidth"), QStringLiteral("interface"),
                   QT_TRANSLATE_NOOP("NexusAI", "侧边栏默认宽度"), 260,
                   PreferenceType::Int, integerRange(180, 560)),
        preference(QStringLiteral("ui.defaultPanelWidth"), QStringLiteral("interface"),
                   QT_TRANSLATE_NOOP("NexusAI", "面板默认宽度"), 300,
                   PreferenceType::Int, integerRange(220, 640)),
        preference(QStringLiteral("ui.defaultSidebarExpanded"), QStringLiteral("interface"),
                   QT_TRANSLATE_NOOP("NexusAI", "侧边栏默认展开"), true,
                   PreferenceType::Bool),
        preference(QStringLiteral("ui.defaultPanelVisible"), QStringLiteral("interface"),
                   QT_TRANSLATE_NOOP("NexusAI", "面板默认展开"), true,
                   PreferenceType::Bool),
        preference(QStringLiteral("ui.restoreWindowOnStartup"), QStringLiteral("interface"),
                   QT_TRANSLATE_NOOP("NexusAI", "启动时恢复窗口"), true,
                   PreferenceType::Bool),
        // docs/31 S4.7 O10：开启后 Theme.motionFast/Base/Slow 归零，32 处
        // Theme.motion* 绑定的过渡动画全部消失（编排型字面量动画不纳入）。
        preference(QStringLiteral("ui.reduceMotion"), QStringLiteral("interface"),
                   QT_TRANSLATE_NOOP("NexusAI", "减少动效"), false,
                   PreferenceType::Bool),
        // docs/31 N2：JSON 对象 actionId → PortableText 序列；"{}" 即全部
        // 默认。动作表本身在 ShortcutMap::actionTable()，这里只负责持久化。
        preference(QStringLiteral("ui.shortcuts"), QStringLiteral("shortcuts"),
                   QT_TRANSLATE_NOOP("NexusAI", "自定义快捷键"),
                   QStringLiteral("{}"), PreferenceType::String),
        // docs/31 S4.1 O8：设置中心窗口尺寸记忆。与 settings.* 同属导航自身
        // 状态：无分类 = 隐藏条目（不进导航/搜索，也不参与复位）；ui/ 前缀
        // 本就不进设置导出。0 = 未保存过，使用默认 900×680。
        preference(QStringLiteral("ui.settingsDialogWidth"), QString(),
                   QT_TRANSLATE_NOOP("NexusAI", "设置窗口宽度"), 0,
                   PreferenceType::Int, integerRange(0, 10000)),
        preference(QStringLiteral("ui.settingsDialogHeight"), QString(),
                   QT_TRANSLATE_NOOP("NexusAI", "设置窗口高度"), 0,
                   PreferenceType::Int, integerRange(0, 10000)),
        preference(QStringLiteral("window.closeToTray"), QStringLiteral("general"),
                   QT_TRANSLATE_NOOP("NexusAI", "关闭时最小化到系统托盘"), false,
                   PreferenceType::Bool),
        preference(QStringLiteral("general.autoStart"), QStringLiteral("general"),
                   QT_TRANSLATE_NOOP("NexusAI", "开机自启动"), false,
                   PreferenceType::Bool),
        preference(QStringLiteral("notice.enabled"), QStringLiteral("general"),
                   QT_TRANSLATE_NOOP("NexusAI", "系统通知"), true,
                   PreferenceType::Bool),
        // docs/31 N1：首次启动向导是否已看过。无分类 = 隐藏条目（不进导航、
        // 搜索与「恢复全部设置」），否则复位设置会把向导重新弹出来。
        // 完成与跳过都写它，保证只打扰一次。
        preference(QStringLiteral("general.onboarded"), QString(),
                   QT_TRANSLATE_NOOP("NexusAI", "首次启动向导已完成"), false,
                   PreferenceType::Bool),
    };
    return values;
}

const QVector<SettingsKeyDef>& SettingsSchema::registeredKeys() {
    static const QVector<SettingsKeyDef> keys = [] {
        const auto intRange = integerRange;
        const auto enumValues = allowedValues;
        QVector<SettingsKeyDef> result = {
            keyDef(QStringLiteral("modelProfiles"), QVariant(), SettingValueType::String),
            // 提示词与指令页的三组用户资产（SettingsService JSON blob）。
            // 可移植用户数据：不加密、不 legacy，随设置备份走通用导出路径。
            keyDef(QStringLiteral("promptPresets"), QVariant(), SettingValueType::String),
            keyDef(QStringLiteral("slashCommands"), QVariant(), SettingValueType::String),
            keyDef(QStringLiteral("sessionTemplates"), QVariant(), SettingValueType::String),
            keyDef(QStringLiteral("activeProfileId"), QString(), SettingValueType::String),
            keyDef(QStringLiteral("contextTokens"), AIAgent::kDefaultContextTokens,
                   SettingValueType::Int,
                   intRange(AIAgent::MinContextTokens, AIAgent::MaxContextTokens)),
            // 2026-11-01：agent/maxSteps 与 agent/taskTimeoutMinutes 已随
            // 「取消任务步数/超时限制」移除——步数只留内部安全上限
            // （kDefaultMaxIterations），任务不再限时。
            // docs/32 P1: 子代理（spawn_agent）。默认关——注册即门控，
            // 开关翻转时 main.cpp 增删工具而非靠禁用层。
            keyDef(QStringLiteral("agent/subagentEnabled"), false, SettingValueType::Bool),
            keyDef(QStringLiteral("agent/subagentMaxParallel"), 3, SettingValueType::Int,
                   intRange(1, 8)),
            // 2026-11-03：内置角色（explore/research/review/coder）的默认
            // 后台开关；预设不存单独档案，由这一键统一控制。
            keyDef(QStringLiteral("agent/subagentPresetBackground"), false,
                   SettingValueType::Bool),
            // 2026-11-01：agent/subagentMaxSteps 与 agent/subagentTimeoutSec
            // 已随「取消子代理步数/超时限制」移除——与主任务一致，模型自定
            // 何时收工（Runner 内部只留防失控安全上限），用户可随时手动停止。
            // docs/33 E2: 子代理角色档案（JSON 资产，{name, prompt, tools[]}）。
            // 同 promptPresets：复合键由专门页面写，不入 setValueForSetting。
            keyDef(QStringLiteral("agent/subagentRoles"), QVariant(), SettingValueType::String),
            keyDef(QStringLiteral("security/allowFullDiskRead"), false, SettingValueType::Bool),
            keyDef(QStringLiteral("security/allowProjectWrite"), false, SettingValueType::Bool),
            keyDef(QStringLiteral("security/localOnlyMode"), false, SettingValueType::Bool),
            keyDef(QStringLiteral("security/allowLocalNetworkFetch"), false, SettingValueType::Bool),
            keyDef(QStringLiteral("embedding/model"), QStringLiteral("nomic-embed-text"),
                   SettingValueType::String, requiredText()),
            keyDef(QStringLiteral("embedding/baseUrl"), QStringLiteral("http://localhost:11434"),
                   SettingValueType::String, requiredText()),
            // 外部向量模型（任务 #16）：embedding 后端选择（本地 ollama ↔
            // OpenAI 兼容协议）与外部服务的 API key（DPAPI 加密，同
            // webSearch/*ApiKey）。键登记纪律：新键必须同步本账本。
            keyDef(QStringLiteral("embedding/provider"), QStringLiteral("ollama"),
                   SettingValueType::String,
                   enumValues({QStringLiteral("ollama"), QStringLiteral("openai")})),
            keyDef(QStringLiteral("embedding/apiKey"), QString(), SettingValueType::String,
                   {}, true),
            keyDef(QStringLiteral("agent/systemPromptOverride"), QString(), SettingValueType::String),
            keyDef(QStringLiteral("logging/level"), QStringLiteral("info"),
                   SettingValueType::String,
                   enumValues({QStringLiteral("debug"), QStringLiteral("info"),
                               QStringLiteral("warning"), QStringLiteral("critical")})),
            keyDef(QStringLiteral("logging/directory"), QString(), SettingValueType::String),
            keyDef(QStringLiteral("logging/maxFileSizeMb"), kDefaultLogMaxFileSizeMb,
                   SettingValueType::Int, intRange(1, 1024)),
            keyDef(QStringLiteral("logging/retentionFiles"), kDefaultLogRetentionFiles,
                   SettingValueType::Int, intRange(1, 100)),
            // 联网搜索后端。合法键由 WebSearchProviderRegistry 决定，这里的
            // 枚举值只是账本的镜像（test_settings 断言两者一致）。默认
            // DuckDuckGo：免密钥，装上即可用。
            keyDef(QStringLiteral("webSearch/provider"),
                   WebSearchProviderRegistry::defaultProviderKey(),
                   SettingValueType::String,
                   enumValues(WebSearchProviderRegistry::keys())),
            // 每个 provider 一把独立密钥：智谱与 DeepSeek 的 key 来自两个不同
            // 平台，共用一个键会导致「配了 A 的 key，切到 B 时把 A 的密钥发给
            // B」——既是 401，也是一次跨厂商凭据泄露。两把 key 都 DPAPI
            // 加密落盘，绝不进设置备份（SettingsBackup 按 encrypted 排除）。
            keyDef(QStringLiteral("webSearch/zhipuApiKey"), QString(), SettingValueType::String,
                   {}, true),
            keyDef(QStringLiteral("webSearch/deepseekApiKey"), QString(), SettingValueType::String,
                   {}, true),
            // H1: outbound HTTP proxy. Applies to every QNetworkAccessManager
            // (LLM / embedding / search / MCP HTTP). The auth blob holds
            // username + password as JSON and is DPAPI-encrypted as a whole,
            // so neither credential is written in plain text.
            keyDef(QStringLiteral("network/proxyMode"), QStringLiteral("system"),
                   SettingValueType::String,
                   enumValues({QStringLiteral("system"), QStringLiteral("manual"),
                               QStringLiteral("off")})),
            keyDef(QStringLiteral("network/proxyHost"), QString(), SettingValueType::String),
            keyDef(QStringLiteral("network/proxyPort"), 8080, SettingValueType::Int,
                   intRange(1, 65535)),
            keyDef(QStringLiteral("network/proxyAuth"), QString(), SettingValueType::String,
                   {}, true),
            keyDef(QStringLiteral("memory/episodicMemoryEnabled"), false, SettingValueType::Bool),
            // docs/23 G3: audit/token retention in days. 0 = keep forever;
            // capped at 10 years to keep the clamp honest.
            keyDef(QStringLiteral("memory/auditRetentionDays"), 30, SettingValueType::Int,
                   intRange(0, 3650)),
            keyDef(QStringLiteral("memory/tokenRetentionDays"), 90, SettingValueType::Int,
                   intRange(0, 3650)),
            // docs/23 D1: startup auto-backup (VACUUM INTO) and rotation.
            keyDef(QStringLiteral("memory/backupOnStartup"), true, SettingValueType::Bool),
            keyDef(QStringLiteral("memory/backupKeepCount"), 3, SettingValueType::Int,
                   intRange(1, 20)),
            // docs/31 S4.2 N6: scheduled backup interval in hours. 0 = off;
            // the service narrows the stored value to 0/1/6/24/168 via
            // BackupSchedule::normalizeBackupIntervalHours().
            keyDef(QStringLiteral("memory/backupIntervalHours"), 0, SettingValueType::Int,
                   intRange(0, 168)),
            keyDef(QStringLiteral("dailyTokenQuota"), 0, SettingValueType::Int,
                   intRange(0, INT_MAX)),
            keyDef(QStringLiteral("dailyCostQuotaCents"), 0, SettingValueType::Int,
                   intRange(0, INT_MAX)),
            keyDef(QStringLiteral("monthlyTokenQuota"), 0, SettingValueType::Int,
                   intRange(0, INT_MAX)),
            keyDef(QStringLiteral("monthlyCostQuotaCents"), 0, SettingValueType::Int,
                   intRange(0, INT_MAX)),
            keyDef(QStringLiteral("strictQuotaMode"), false, SettingValueType::Bool),
            // docs/31 S4.1 N9：用量达到配额的该百分比时提醒一次（0 = 关闭）。
            // 只在"未超限"区间生效；超限仍走 strictQuotaMode / quotaWarning。
            keyDef(QStringLiteral("usage/alertPercent"), 80, SettingValueType::Int,
                   intRange(0, 100)),
            keyDef(QStringLiteral("agent/defaultPermissionMode"), QStringLiteral("confirm"),
                   SettingValueType::String,
                   enumValues({QStringLiteral("confirm"), QStringLiteral("auto_edit"),
                               QStringLiteral("full_access"), QStringLiteral("plan_only")})),
            keyDef(QStringLiteral("ui/sidebarWidth"), QVariant(), SettingValueType::Int,
                   intRange(180, 560)),
            keyDef(QStringLiteral("ui/panelWidth"), QVariant(), SettingValueType::Int,
                   intRange(220, 640)),
            keyDef(QStringLiteral("ui/panelVisible"), QVariant(), SettingValueType::Bool),
            keyDef(QStringLiteral("ui/sidebarExpanded"), QVariant(), SettingValueType::Bool),
            // 会话过程（思考/工具调用/工具结果/context）是否展开显示。
            // 默认 true：折叠会把"正在做什么"藏起来，首次使用更需要看到它；
            // 用户嫌吵可以关掉，选择随下次启动保留。
            keyDef(QStringLiteral("ui/showProcess"), true, SettingValueType::Bool),
            keyDef(QStringLiteral("ui/windowX"), QVariant(), SettingValueType::Double),
            keyDef(QStringLiteral("ui/windowY"), QVariant(), SettingValueType::Double),
            keyDef(QStringLiteral("ui/windowWidth"), QVariant(), SettingValueType::Double),
            keyDef(QStringLiteral("ui/windowHeight"), QVariant(), SettingValueType::Double),
            keyDef(QStringLiteral("ui/windowMaximized"), false, SettingValueType::Bool),
            keyDef(QStringLiteral("tools/disabled"), QStringList(), SettingValueType::StringList),
            keyDef(QStringLiteral("schemaVersion"), 0, SettingValueType::Int),
            keyDef(QStringLiteral("projects/activeId"), QString(), SettingValueType::String),
            // docs/37 W2：Skill 能力开关。本期只登记键；读取方与设置页在 W3
            // 落地（不新增 SettingsService 属性，键直接走 schema 默认值）。
            keyDef(QStringLiteral("skills/enabled"), true, SettingValueType::Bool),
            keyDef(QStringLiteral("skills/projectEnabled"), true, SettingValueType::Bool),
            keyDef(QStringLiteral("skills/maxPerMessage"), 3, SettingValueType::Int,
                   intRange(1, 5)),
            keyDef(QStringLiteral("security/readDenyList"), QVariantList(), SettingValueType::Any),
            keyDef(QStringLiteral("theme/dark"), false, SettingValueType::Bool, {}, false, true),
        };

        for (const PreferenceMeta& meta : preferences()) {
            result.append(keyDef(QStringLiteral("prefs/v1/") + meta.key,
                                 meta.defaultValue, valueType(meta.type),
                                 meta.constraint));
        }
        return result;
    }();
    return keys;
}

const SettingsKeyDef* SettingsSchema::definition(const QString& key) {
    const auto& keys = registeredKeys();
    for (const SettingsKeyDef& def : keys)
        if (def.key == key) return &def;
    return nullptr;
}

QVariant SettingsSchema::defaultValue(const QString& key, const QVariant& fallback) {
    const SettingsKeyDef* def = definition(key);
    return def ? def->defaultValue : fallback;
}

int SettingsSchema::clampInt(const QString& key, int value) {
    const SettingsKeyDef* def = definition(key);
    if (!def || def->type != SettingValueType::Int || !def->constraint.hasRange)
        return value;
    return qBound(def->constraint.min, value, def->constraint.max);
}

QStringList SettingsSchema::options(const QString& key) {
    const SettingsKeyDef* def = definition(key);
    return def ? def->constraint.options : QStringList();
}

bool SettingsSchema::isValidValue(const QString& key, const QVariant& value) {
    const SettingsKeyDef* def = definition(key);
    if (!def) return false;
    if (!value.isValid()) return !def->defaultValue.isValid();

    const int typeId = value.typeId();
    const bool numeric = typeId == QMetaType::SChar || typeId == QMetaType::UChar
        || typeId == QMetaType::Short || typeId == QMetaType::UShort
        || typeId == QMetaType::Int || typeId == QMetaType::UInt
        || typeId == QMetaType::Long || typeId == QMetaType::ULong
        || typeId == QMetaType::LongLong || typeId == QMetaType::ULongLong
        || typeId == QMetaType::Float || typeId == QMetaType::Double;
    bool ok = true;
    switch (def->type) {
    case SettingValueType::Any:
        return true;
    case SettingValueType::Bool:
        ok = value.typeId() == QMetaType::Bool;
        break;
    case SettingValueType::Int: {
        if (!numeric) return false;
        const double converted = value.toDouble(&ok);
        ok = ok && std::isfinite(converted) && std::floor(converted) == converted
            && converted >= std::numeric_limits<int>::min()
            && converted <= std::numeric_limits<int>::max();
        if (ok && def->constraint.hasRange)
            ok = converted >= def->constraint.min && converted <= def->constraint.max;
        break;
    }
    case SettingValueType::Double: {
        if (!numeric) return false;
        const double converted = value.toDouble(&ok);
        ok = ok && std::isfinite(converted);
        if (ok && def->constraint.hasRange)
            ok = converted >= def->constraint.min && converted <= def->constraint.max;
        break;
    }
    case SettingValueType::String:
        ok = typeId == QMetaType::QString;
        if (ok && def->constraint.required) ok = !value.toString().isEmpty();
        if (ok && def->constraint.maxLength > 0)
            ok = value.toString().size() <= def->constraint.maxLength;
        break;
    case SettingValueType::StringList:
        ok = typeId == QMetaType::QStringList;
        if (!ok && typeId == QMetaType::QVariantList) {
            ok = true;
            for (const QVariant& item : value.toList()) {
                if (item.typeId() != QMetaType::QString) {
                    ok = false;
                    break;
                }
            }
        }
        break;
    }
    if (!ok) return false;
    if (!def->constraint.options.isEmpty()) {
        const bool isDefault = def->defaultValue.isValid()
            && value.toString() == def->defaultValue.toString();
        return isDefault || def->constraint.options.contains(value.toString());
    }
    return true;
}

int SettingsSchema::intSetting(const QString& key) {
    return clampInt(key,
        SettingsRepository::value(key, defaultValue(key)).toInt());
}

bool SettingsSchema::boolSetting(const QString& key) {
    return SettingsRepository::value(key, defaultValue(key)).toBool();
}

} // namespace NexusAI
