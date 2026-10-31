#include "SettingsValueFormat.h"

#include "PreferenceRegistry.h"
#include "PreferenceStore.h"
#include "SettingsService.h"
#include "core/utils/SettingsRepository.h"
#include "core/utils/SettingsSchema.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

namespace NexusAI {
namespace {

QString valueText(const char* source) {
    return QCoreApplication::translate("SettingsValue", source);
}

// 代理凭据没有独立存储：用户名与密码整体存在加密的 network/proxyAuth
// blob 里，这两条目录项只是页面锚点。按凭据处理，避免出现"已修改"却没
// 有任何可复原目标的行。
bool isCredentialAnchor(const QString& key) {
    return key == QLatin1String("network/proxyUsername")
        || key == QLatin1String("network/proxyPassword");
}

// 联网搜索的输入框同样只是锚点：真正的存储键按 provider 分桶
// （webSearch/<provider>ApiKey），页面上的「API Key」行需要跟随当前
// provider 回答"配没配"，而不是因为找不到同名 schema 键就显示空白。
bool isWebSearchKeyAnchor(const QString& key) {
    return key == QLatin1String("webSearch/apiKey");
}

bool isSecretAnchor(const QString& key) {
    return isCredentialAnchor(key) || isWebSearchKeyAnchor(key);
}

enum class DisplayType { Bool, Int, String, StringList, Other };

DisplayType displayTypeOf(PreferenceType type) {
    switch (type) {
    case PreferenceType::Bool: return DisplayType::Bool;
    case PreferenceType::Int: return DisplayType::Int;
    case PreferenceType::Enum:
    case PreferenceType::String: return DisplayType::String;
    }
    return DisplayType::String;
}

DisplayType displayTypeOf(SettingValueType type) {
    switch (type) {
    case SettingValueType::Bool: return DisplayType::Bool;
    case SettingValueType::Int: return DisplayType::Int;
    case SettingValueType::Double: return DisplayType::Other;
    case SettingValueType::String: return DisplayType::String;
    case SettingValueType::StringList: return DisplayType::StringList;
    case SettingValueType::Any: return DisplayType::Other;
    }
    return DisplayType::Other;
}

DisplayType displayTypeForKey(const QString& key) {
    if (auto* registry = PreferenceRegistry::instance(); registry
            && registry->hasPreference(key)) {
        if (const PreferenceMeta* meta = registry->meta(key))
            return displayTypeOf(meta->type);
        return DisplayType::Other;
    }
    if (const SettingsKeyDef* def = SettingsSchema::definition(key))
        return displayTypeOf(def->type);
    return DisplayType::Other;
}

bool sameValue(DisplayType type, const QVariant& value, const QVariant& expected) {
    switch (type) {
    case DisplayType::Bool: return value.toBool() == expected.toBool();
    case DisplayType::Int: return value.toInt() == expected.toInt();
    case DisplayType::String: return value.toString() == expected.toString();
    case DisplayType::StringList: return value.toStringList() == expected.toStringList();
    case DisplayType::Other:
        // Double / Any（如 security/readDenyList 的 QVariantList）没有统一
        // 的字符串形式：QVariant(QVariantList).toString() 恒为空，用它比较
        // 会让"追加了黑名单"永远显示未修改。同类型直接比 QVariant，
        // 跨类型才退回字符串，避免 Double 与 Int 互比时误判。
        if (value.metaType() == expected.metaType()) return value == expected;
        return value.toString() == expected.toString();
    }
    return value == expected;
}

// 枚举值 → 本地化标签。只收录设置中心实际展示的枚举键；未收录的值原样
// 显示（稳定 token 仍然可读，例如 provider 的厂商名）。
QString enumLabel(const QString& key, const QString& value) {
    if (value.isEmpty()) return {};
    struct Entry {
        const char* key;
        const char* value;
        const char* label;
    };
    static const Entry kLabels[] = {
        {"ui.themeMode", "auto", QT_TRANSLATE_NOOP("SettingsValue", "跟随系统")},
        {"ui.themeMode", "dark", QT_TRANSLATE_NOOP("SettingsValue", "深色")},
        {"ui.themeMode", "light", QT_TRANSLATE_NOOP("SettingsValue", "浅色")},
        {"network/proxyMode", "system", QT_TRANSLATE_NOOP("SettingsValue", "跟随系统")},
        {"network/proxyMode", "manual", QT_TRANSLATE_NOOP("SettingsValue", "手动")},
        {"network/proxyMode", "off", QT_TRANSLATE_NOOP("SettingsValue", "关闭")},
        {"logging/level", "debug", QT_TRANSLATE_NOOP("SettingsValue", "调试")},
        {"logging/level", "info", QT_TRANSLATE_NOOP("SettingsValue", "信息")},
        {"logging/level", "warning", QT_TRANSLATE_NOOP("SettingsValue", "警告")},
        {"logging/level", "critical", QT_TRANSLATE_NOOP("SettingsValue", "严重")},
        {"agent/defaultPermissionMode", "confirm",
         QT_TRANSLATE_NOOP("SettingsValue", "每次都确认")},
        {"agent/defaultPermissionMode", "auto_edit",
         QT_TRANSLATE_NOOP("SettingsValue", "自动编辑")},
        {"agent/defaultPermissionMode", "full_access",
         QT_TRANSLATE_NOOP("SettingsValue", "完全访问")},
        {"agent/defaultPermissionMode", "plan_only",
         QT_TRANSLATE_NOOP("SettingsValue", "仅计划")},
        {"embedding/provider", "ollama", QT_TRANSLATE_NOOP("SettingsValue", "本地 Ollama")},
        {"embedding/provider", "openai", QT_TRANSLATE_NOOP("SettingsValue", "OpenAI 兼容")},
    };
    for (const Entry& entry : kLabels) {
        if (key == QLatin1String(entry.key)
            && value == QLatin1String(entry.value))
            return valueText(entry.label);
    }
    return {};
}

QString truncate(const QString& text, int limit = 32) {
    const QString trimmed = text.simplified();
    if (trimmed.size() <= limit) return trimmed;
    return trimmed.left(limit) + QStringLiteral("…");
}

} // namespace

namespace SettingsValue {

SettingValueSnapshot inspect(const QString& key) {
    SettingValueSnapshot snapshot;
    if (key.isEmpty()) return snapshot;

    // 声明式偏好：默认值由 PreferenceRegistry 提供（可能与 SettingsSchema
    // 在运行期同步过，例如字体族在 FontProbe 探测后才填）。
    if (auto* registry = PreferenceRegistry::instance(); registry
            && registry->hasPreference(key)) {
        const PreferenceMeta* meta = registry->meta(key);
        if (!meta) return snapshot;
        snapshot.known = true;
        snapshot.defaultValue = meta->defaultValue;
        snapshot.stored = PreferenceStore::instance()->hasStoredValue(key);
        snapshot.current = PreferenceStore::instance()->load(key);
        snapshot.modified = snapshot.stored
            && !sameValue(displayTypeOf(meta->type), snapshot.current,
                          snapshot.defaultValue);
        return snapshot;
    }

    // 锚点先于 schema 处理：它们没有同名存储键，但确实是用户可见的密钥行。
    if (isSecretAnchor(key)) {
        snapshot.known = true;
        snapshot.secret = true;
        if (isCredentialAnchor(key)) {
            // 代理凭据看加密 blob 是否非空。
            snapshot.stored = SettingsRepository::contains(
                QStringLiteral("network/proxyAuth"));
            if (snapshot.stored) {
                snapshot.configured = !SettingsRepository::value(
                    QStringLiteral("network/proxyAuth")).toString().isEmpty();
            }
        } else {
            // 联网搜索跟随当前 provider；密钥明文只在服务内存里。
            snapshot.configured = SettingsService::instance()->webSearchApiKeySet();
            snapshot.stored = snapshot.configured;
        }
        return snapshot;
    }

    const SettingsKeyDef* def = SettingsSchema::definition(key);
    if (!def) return snapshot;

    snapshot.known = true;
    snapshot.defaultValue = def->defaultValue;
    snapshot.secret = def->encrypted;

    if (snapshot.secret) {
        // 密钥只回答"有没有"：加密键看落盘值非空。
        snapshot.stored = SettingsRepository::contains(key);
        if (snapshot.stored) {
            const QVariant raw = SettingsRepository::value(key);
            snapshot.configured = !raw.toString().isEmpty();
        }
        return snapshot;
    }

    snapshot.stored = SettingsRepository::contains(key);
    snapshot.current = SettingsRepository::value(key, def->defaultValue);
    snapshot.modified = snapshot.stored
        && !sameValue(displayTypeOf(def->type), snapshot.current, def->defaultValue);
    return snapshot;
}

QString displayOf(const SettingValueSnapshot& snapshot, const QString& key) {
    if (!snapshot.known) return {};
    if (snapshot.secret)
        return snapshot.configured ? valueText(QT_TRANSLATE_NOOP("SettingsValue", "已配置"))
                                   : valueText(QT_TRANSLATE_NOOP("SettingsValue", "未配置"));

    // docs/31 N2：ui.shortcuts 是 JSON 对象，展示原文又长又难读。目录、
    // 搜索与复位确认统一显示覆盖项数量。
    if (key == QLatin1String("ui.shortcuts")) {
        const QJsonDocument doc =
            QJsonDocument::fromJson(snapshot.current.toString().toUtf8());
        const int count = doc.isObject() ? doc.object().size() : 0;
        if (count == 0)
            return valueText(QT_TRANSLATE_NOOP("SettingsValue", "全部默认"));
        return valueText(QT_TRANSLATE_NOOP("SettingsValue", "%1 项自定义")).arg(count);
    }

    const DisplayType type = displayTypeForKey(key);

    switch (type) {
    case DisplayType::Bool:
        return valueText(snapshot.current.toBool()
                             ? QT_TRANSLATE_NOOP("SettingsValue", "开")
                             : QT_TRANSLATE_NOOP("SettingsValue", "关"));
    case DisplayType::Int:
        return QString::number(snapshot.current.toInt());
    case DisplayType::StringList:
        return valueText(QT_TRANSLATE_NOOP("SettingsValue", "%1 项"))
            .arg(snapshot.current.toStringList().size());
    case DisplayType::String:
        if (const QString label = enumLabel(key, snapshot.current.toString());
            !label.isEmpty())
            return label;
        if (snapshot.current.toString().trimmed().isEmpty())
            return valueText(QT_TRANSLATE_NOOP("SettingsValue", "（空）"));
        return truncate(snapshot.current.toString());
    case DisplayType::Other:
        break;
    }

    const QVariant& value = snapshot.current;
    if (value.canConvert<QVariantList>()) {
        const QVariantList list = value.toList();
        if (!list.isEmpty())
            return valueText(QT_TRANSLATE_NOOP("SettingsValue", "%1 项")).arg(list.size());
    }
    const QString text = truncate(value.toString());
    if (text.isEmpty())
        return valueText(QT_TRANSLATE_NOOP("SettingsValue", "（空）"));
    return text;
}

QString displayValue(const QString& key, const QVariant& value) {
    SettingValueSnapshot snapshot = inspect(key);
    if (!snapshot.known) return truncate(value.toString());
    // 密钥只回答"有没有"，不渲染传入的任何值（docs/31 §6 红线 1）。
    if (snapshot.secret) return displayOf(snapshot, key);
    snapshot.current = value;
    snapshot.stored = true;
    return displayOf(snapshot, key);
}

bool sameValueForKey(const QString& key, const QVariant& value,
                     const QVariant& expected) {
    return sameValue(displayTypeForKey(key), value, expected);
}

} // namespace SettingsValue
} // namespace NexusAI
