#pragma once

#include <QString>
#include <QVariant>
#include <QVector>

namespace NexusAI {

// L2: centralized registry of all settings keys, their defaults, and flags.
// This is the single source of truth — SettingsService reads/writes these
// keys, test SettingsGuard backs them up, and future migrations reference
// this list. B档 (decision D1=B): registry + schemaVersion + migration hook,
// no import/export.
//
// docs/17 B3: 账本范围 = 所有 QSettings("NexusAI","NexusAI") 键，含三类旁路
// 写入方（ToolRegistry 的 tools/disabled、AgentViewModel 的运行时布局键、
// PreferenceStore 的 prefs/v1/* 物理键）。MCP 服务器配置（mcp_servers.json）
// 为文件存储，不属于本账本。账本完整性由 test_settings 强制。
struct SettingsKeyDef {
    QString key;
    QVariant defaultValue;
    bool encrypted; // DPAPI at rest (never logged, never exported)
};

class SettingsSchema {
public:
    // Bumped when a migration renames/removes keys. v0 = pre-schema (all
    // keys had defaults; no structural change at v1).
    static constexpr int kSchemaVersion = 1;

    // All registered settings keys (global + UI + theme). Profile-internal
    // keys (provider/apiKey/model/baseUrl inside the modelProfiles JSON) are
    // NOT listed here — they live inside the modelProfiles blob, not as
    // top-level QSettings keys.
    static const QVector<SettingsKeyDef>& registeredKeys();

    // Migration hook: called at startup with the stored schema version.
    // v0 → v1 is a no-op (keys already have defaults); future schema bumps
    // perform renames/removals here.
    static void migrate(int fromVersion);
};

} // namespace NexusAI
