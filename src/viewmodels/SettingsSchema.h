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
    // keys had defaults); v1 = 初版结构化 schema；v2 = 模型列表改为用户手动
    // 维护（存量 profile 的 model/models 在 SettingsService::load 里清空）；
    // v3 = contextTokens 默认值 8192 → 300000（存量等于旧默认的值随之上调，
    // 见 SettingsService::upgradeContextTokenDefault）。
    static constexpr int kSchemaVersion = 3;

    // All registered settings keys (global + UI + theme). Profile-internal
    // keys (provider/apiKey/model/baseUrl inside the modelProfiles JSON) are
    // NOT listed here — they live inside the modelProfiles blob, not as
    // top-level QSettings keys.
    static const QVector<SettingsKeyDef>& registeredKeys();

    // Migration hook: called at startup with the stored schema version.
    // v0 → v1 is a no-op (keys already have defaults); v1 → v2 需要改写
    // modelProfiles blob（不在本表的顶层键范围内），因此由
    // SettingsService::load() 依同一个 storedVersion 完成。
    static void migrate(int fromVersion);
};

} // namespace NexusAI
