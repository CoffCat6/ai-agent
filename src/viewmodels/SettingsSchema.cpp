#include "SettingsSchema.h"

namespace NexusAI {

const QVector<SettingsKeyDef>& SettingsSchema::registeredKeys() {
    static const QVector<SettingsKeyDef> keys = {
        {"modelProfiles",                 QVariant(),                        false},
        {"activeProfileId",               QVariant(),                        false},
        {"contextTokens",                 8192,                              false},
        {"simulationMode",                false,                             false},
        {"security/allowFullDiskRead",    false,                             false},
        {"security/localOnlyMode",        false,                             false},
        {"security/allowLocalNetworkFetch", false,                           false},
        {"embedding/model",               QStringLiteral("nomic-embed-text"), false},
        {"embedding/baseUrl",             QStringLiteral("http://localhost:11434"), false},
        {"agent/systemPromptOverride",    QString(),                         false},
        {"logging/level",                 QStringLiteral("info"),            false},
        {"webSearch/provider",            QStringLiteral("tavily"),           false},
        {"webSearch/apiKey",              QString(),                         true},  // DPAPI
        {"memory/episodicMemoryEnabled",  false,                             false},
        // docs/16 §7.4: quota control keys (flat, no prefix). Int quotas
        // default to 0 = unlimited; strictQuotaMode default false = warn only.
        {"dailyTokenQuota",               0,                                  false},
        {"dailyCostQuotaCents",           0,                                  false},
        {"monthlyTokenQuota",             0,                                  false},
        {"monthlyCostQuotaCents",         0,                                  false},
        {"strictQuotaMode",               false,                             false},
        // 会话权限模式全局默认（confirm/auto_edit/full_access），新 tab 初始值。
        {"agent/defaultPermissionMode",   QStringLiteral("confirm"),         false},
        {"ui/sidebarWidth",               QVariant(),                        false},
        {"ui/panelWidth",                 QVariant(),                        false},
        {"ui/panelVisible",               QVariant(),                        false},
        {"ui/sidebarExpanded",            QVariant(),                        false},
        {"prefs/v1/ui.themeMode",         QStringLiteral("auto"),            false},

        // ── docs/17 B3: 账本收编——此前"唯一事实源"漏登记的键 ──
        // M3: ToolRegistry 直写（用户禁用清单，QStringList）。
        {"tools/disabled",                QVariant(),                        false},
        // 迁移钩子自持（load() 写入 kSchemaVersion），无静态默认。
        {"schemaVersion",                 QVariant(),                        false},
        // M2: PreferenceStore 物理键（prefs/v1/<key>）。字体默认值取自
        // 系统探测（ThemeManager），无静态默认；空 locale = 跟随系统。
        {"prefs/v1/ui.fontSans",          QVariant(),                        false},
        {"prefs/v1/ui.fontMono",          QVariant(),                        false},
        {"prefs/v1/ui.baseFontSize",      14,                                false},
        {"prefs/v1/ui.locale",            QVariant(),                        false},
        {"prefs/v1/ui.defaultSidebarWidth",  260,                            false},
        {"prefs/v1/ui.defaultPanelWidth",    300,                            false},
        {"prefs/v1/ui.defaultSidebarExpanded", true,                         false},
        {"prefs/v1/ui.defaultPanelVisible",  true,                           false},
        {"prefs/v1/ui.restoreWindowOnStartup", true,                         false},
        // M2 内部键：设置中心记住上次访问分类。
        {"settings.lastCategory",         QVariant(),                        false},
        // docs/17 A0: 当前活动项目 id（空 = 无项目，本地会话）。
        {"projects/activeId",             QString(),                         false},
        // docs/17 A1: 读取黑名单用户追加项（目录或 glob；内置默认清单
        // 在 Sandbox::defaultDenyList，不在此存储）。
        {"security/readDenyList",         QVariant(),                        false},
    };
    return keys;
}

void SettingsSchema::migrate(int fromVersion) {
    // v0 → v1: no-op. All keys have defaults; no renames or removals.
    // The v1 registry (registeredKeys()) includes the docs/16 §7.4 quota keys
    // (dailyTokenQuota/dailyCostQuotaCents/monthlyTokenQuota/monthlyCostQuotaCents/
    // strictQuotaMode); they carry defaults (0/unlimited, false/warn-only) so
    // existing installs pick them up on next load without a data migration.
    // Future: if (fromVersion < 2) { ... rename old keys ... }
    (void)fromVersion;
}

} // namespace NexusAI
