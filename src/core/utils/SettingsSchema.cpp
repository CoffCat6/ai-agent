#include "SettingsSchema.h"

#include "core/agent/AIAgent.h"
#include "core/utils/SettingsRepository.h"

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

const QVector<CategoryMeta>& SettingsSchema::categories() {
    static const QVector<CategoryMeta> values = {
        {QStringLiteral("model"), QT_TRANSLATE_NOOP("NexusAI", "模型配置"), 1},
        {QStringLiteral("interface"), QT_TRANSLATE_NOOP("NexusAI", "界面设置"), 2},
        {QStringLiteral("general"), QT_TRANSLATE_NOOP("NexusAI", "通用设置"), 3},
        {QStringLiteral("security"), QT_TRANSLATE_NOOP("NexusAI", "安全"), 4},
        {QStringLiteral("mcp"), QT_TRANSLATE_NOOP("NexusAI", "MCP 服务器"), 5},
        {QStringLiteral("tools"), QT_TRANSLATE_NOOP("NexusAI", "工具管理"), 6},
        {QStringLiteral("memory"), QT_TRANSLATE_NOOP("NexusAI", "记忆管理"), 7},
        {QStringLiteral("usage"), QT_TRANSLATE_NOOP("NexusAI", "用量统计"), 8},
        {QStringLiteral("activity"), QT_TRANSLATE_NOOP("NexusAI", "活动记录"), 9},
        {QStringLiteral("backup"), QT_TRANSLATE_NOOP("NexusAI", "设置与备份"), 10},
    };
    return values;
}

const QVector<PreferenceMeta>& SettingsSchema::preferences() {
    static const QVector<PreferenceMeta> values = {
        preference(QStringLiteral("settings.lastCategory"), QString(),
                   QT_TRANSLATE_NOOP("NexusAI", "上次访问分类"), QStringLiteral("model"),
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
        preference(QStringLiteral("window.closeToTray"), QStringLiteral("general"),
                   QT_TRANSLATE_NOOP("NexusAI", "关闭时最小化到系统托盘"), false,
                   PreferenceType::Bool),
        preference(QStringLiteral("general.autoStart"), QStringLiteral("general"),
                   QT_TRANSLATE_NOOP("NexusAI", "开机自启动"), false,
                   PreferenceType::Bool),
        preference(QStringLiteral("notice.enabled"), QStringLiteral("general"),
                   QT_TRANSLATE_NOOP("NexusAI", "系统通知"), true,
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
            keyDef(QStringLiteral("activeProfileId"), QString(), SettingValueType::String),
            keyDef(QStringLiteral("contextTokens"), AIAgent::kDefaultContextTokens,
                   SettingValueType::Int,
                   intRange(AIAgent::MinContextTokens, AIAgent::MaxContextTokens)),
            keyDef(QStringLiteral("agent/maxSteps"), AIAgent::kDefaultMaxIterations,
                   SettingValueType::Int,
                   intRange(AIAgent::kMinMaxIterations, AIAgent::kIterationCeiling)),
            keyDef(QStringLiteral("agent/taskTimeoutMinutes"),
                   AIAgent::kDefaultTaskTimeoutMs / 60000, SettingValueType::Int,
                   intRange(1, 60)),
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
            // webSearch/apiKey）。键登记纪律：新键必须同步本账本。
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
            keyDef(QStringLiteral("webSearch/provider"), QStringLiteral("tavily"),
                   SettingValueType::String,
                   enumValues({QStringLiteral("tavily"), QStringLiteral("duckduckgo"),
                               QStringLiteral("searxng")})),
            keyDef(QStringLiteral("webSearch/apiKey"), QString(), SettingValueType::String,
                   {}, true),
            keyDef(QStringLiteral("webSearch/searxBaseUrl"), QString(), SettingValueType::String),
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
            keyDef(QStringLiteral("dailyTokenQuota"), 0, SettingValueType::Int,
                   intRange(0, INT_MAX)),
            keyDef(QStringLiteral("dailyCostQuotaCents"), 0, SettingValueType::Int,
                   intRange(0, INT_MAX)),
            keyDef(QStringLiteral("monthlyTokenQuota"), 0, SettingValueType::Int,
                   intRange(0, INT_MAX)),
            keyDef(QStringLiteral("monthlyCostQuotaCents"), 0, SettingValueType::Int,
                   intRange(0, INT_MAX)),
            keyDef(QStringLiteral("strictQuotaMode"), false, SettingValueType::Bool),
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
            keyDef(QStringLiteral("ui/windowX"), QVariant(), SettingValueType::Double),
            keyDef(QStringLiteral("ui/windowY"), QVariant(), SettingValueType::Double),
            keyDef(QStringLiteral("ui/windowWidth"), QVariant(), SettingValueType::Double),
            keyDef(QStringLiteral("ui/windowHeight"), QVariant(), SettingValueType::Double),
            keyDef(QStringLiteral("ui/windowMaximized"), false, SettingValueType::Bool),
            keyDef(QStringLiteral("tools/disabled"), QStringList(), SettingValueType::StringList),
            keyDef(QStringLiteral("schemaVersion"), 0, SettingValueType::Int),
            keyDef(QStringLiteral("projects/activeId"), QString(), SettingValueType::String),
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
