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
        {"ui/sidebarWidth",               QVariant(),                        false},
        {"ui/panelWidth",                 QVariant(),                        false},
        {"ui/panelVisible",               QVariant(),                        false},
        {"ui/sidebarExpanded",            QVariant(),                        false},
        {"prefs/v1/ui.themeMode",         QStringLiteral("auto"),            false},
    };
    return keys;
}

void SettingsSchema::migrate(int fromVersion) {
    // v0 → v1: no-op. All keys have defaults; no renames or removals.
    // Future: if (fromVersion < 2) { ... rename old keys ... }
    (void)fromVersion;
}

} // namespace NexusAI
