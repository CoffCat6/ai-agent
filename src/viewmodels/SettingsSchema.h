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
