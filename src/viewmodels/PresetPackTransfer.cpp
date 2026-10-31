#include "PresetPackTransfer.h"

#include "SettingsService.h"
#include "core/tools/ToolRegistry.h"
#include "core/utils/SettingsSchema.h"

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <QUrl>

namespace NexusAI {
namespace {

constexpr auto kFormat = "nexus-preset-pack";
constexpr int kVersion = 1;
constexpr qint64 kMaxPackBytes = 2 * 1024 * 1024;
constexpr int kMaxEntries = 500;
constexpr int kMaxPreviewConflicts = 200;

QString localPath(const QString& path) {
    const QUrl url(path);
    return url.isLocalFile() ? url.toLocalFile() : path;
}

QString newId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// 指令词与 SettingsService::addSlashCommand 同一口径：去前导 '/'、转小写。
QString normalizedCommandName(const QString& raw) {
    QString name = raw.trimmed();
    while (name.startsWith(QLatin1Char('/'))) name.remove(0, 1);
    return name.toLower();
}

QString uniquePresetName(const QString& base, const QSet<QString>& used) {
    QString candidate = base;
    for (int n = 2; used.contains(candidate.toCaseFolded()); ++n)
        candidate = QStringLiteral("%1 (%2)").arg(base).arg(n);
    return candidate;
}

QString uniqueCommandName(const QString& base, const QSet<QString>& used) {
    QString candidate = base;
    for (int n = 2; used.contains(candidate); ++n)
        candidate = QStringLiteral("%1-%2").arg(base).arg(n);
    return candidate;
}

} // namespace

PresetPackTransfer* PresetPackTransfer::s_instance = nullptr;

PresetPackTransfer::PresetPackTransfer(QObject* parent) : QObject(parent) {}

PresetPackTransfer* PresetPackTransfer::instance() {
    if (!s_instance) s_instance = new PresetPackTransfer;
    return s_instance;
}

PresetPackTransfer* PresetPackTransfer::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

void PresetPackTransfer::setError(const QString& error) {
    if (m_lastError == error) return;
    m_lastError = error;
    emit lastErrorChanged();
}

QVariantMap PresetPackTransfer::parsePack(const QString& path, Pack* pack) {
    const auto fail = [this](const QString& error) -> QVariantMap {
        setError(error);
        return {{QStringLiteral("valid"), false},
                {QStringLiteral("error"), error}};
    };

    QFile file(localPath(path));
    if (!file.open(QIODevice::ReadOnly))
        return fail(tr("无法读取预设包：%1").arg(file.errorString()));
    if (file.size() > kMaxPackBytes) return fail(tr("预设包超过 2 MB 限制"));

    QJsonParseError parseError{};
    const QJsonDocument doc =
        QJsonDocument::fromJson(file.read(kMaxPackBytes + 1), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return fail(tr("预设包不是有效的 JSON 对象"));

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String(kFormat)
        || root.value(QStringLiteral("version")).toInt(-1) != kVersion)
        return fail(tr("不支持的预设包格式或版本"));
    if (!root.value(QStringLiteral("presets")).isArray()
        || !root.value(QStringLiteral("commands")).isArray())
        return fail(tr("预设包缺少预设或指令列表"));

    *pack = Pack{};
    QSet<QString> presetNames;
    for (const QJsonValue& value : root.value(QStringLiteral("presets")).toArray()) {
        if (!value.isObject()) return fail(tr("预设条目格式错误"));
        const QJsonObject entry = value.toObject();
        const QString name = entry.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty() || !entry.value(QStringLiteral("prompt")).isString())
            return fail(tr("预设条目缺少名称或内容"));
        if (presetNames.contains(name.toCaseFolded()))
            return fail(tr("预设包包含重名预设：%1").arg(name));
        if (pack->presets.size() >= kMaxEntries)
            return fail(tr("预设数量超过 %1 条限制").arg(kMaxEntries));
        presetNames.insert(name.toCaseFolded());
        pack->presets.append(QJsonObject{
            {QStringLiteral("name"), name},
            {QStringLiteral("prompt"), entry.value(QStringLiteral("prompt")).toString()}});
    }

    QSet<QString> commandNames;
    for (const QJsonValue& value : root.value(QStringLiteral("commands")).toArray()) {
        if (!value.isObject()) return fail(tr("指令条目格式错误"));
        const QJsonObject entry = value.toObject();
        const QString name =
            normalizedCommandName(entry.value(QStringLiteral("name")).toString());
        const QString templ = entry.value(QStringLiteral("templ")).toString();
        if (name.isEmpty() || templ.trimmed().isEmpty())
            return fail(tr("指令条目缺少指令词或模板"));
        if (commandNames.contains(name))
            return fail(tr("预设包包含重复指令：/%1").arg(name));
        if (pack->commands.size() >= kMaxEntries)
            return fail(tr("指令数量超过 %1 条限制").arg(kMaxEntries));
        commandNames.insert(name);
        pack->commands.append(QJsonObject{
            {QStringLiteral("name"), name},
            {QStringLiteral("templ"), templ},
            {QStringLiteral("note"),
             entry.value(QStringLiteral("note")).toString()}});
    }

    const QJsonValue tools = root.value(QStringLiteral("disabledTools"));
    if (!tools.isUndefined() && !tools.isArray())
        return fail(tr("禁用工具列表格式错误"));
    QSet<QString> toolSeen;
    for (const QJsonValue& value : tools.toArray()) {
        if (!value.isString()) return fail(tr("禁用工具列表格式错误"));
        const QString name = value.toString().trimmed();
        if (name.isEmpty() || toolSeen.contains(name)) continue;
        if (pack->disabledTools.size() >= kMaxEntries)
            return fail(tr("禁用工具数量超过 %1 条限制").arg(kMaxEntries));
        toolSeen.insert(name);
        pack->disabledTools.append(name);
    }

    const QJsonValue mode = root.value(QStringLiteral("permissionMode"));
    if (!mode.isUndefined() && !mode.isString())
        return fail(tr("默认权限模式格式错误"));
    pack->permissionMode = mode.toString().trimmed();
    if (!pack->permissionMode.isEmpty()
        && !SettingsSchema::options(QStringLiteral("agent/defaultPermissionMode"))
               .contains(pack->permissionMode))
        return fail(tr("预设包包含未知的默认权限模式：%1").arg(pack->permissionMode));

    return {{QStringLiteral("valid"), true}, {QStringLiteral("error"), QString()}};
}

bool PresetPackTransfer::exportPack(const QString& path) {
    setError({});
    auto* service = SettingsService::instance();

    QJsonArray presets;
    for (const QVariant& value : service->promptPresets()) {
        const QVariantMap entry = value.toMap();
        const QString name = entry.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty()) continue;
        presets.append(QJsonObject{
            {QStringLiteral("name"), name},
            {QStringLiteral("prompt"),
             entry.value(QStringLiteral("prompt")).toString()}});
    }

    QJsonArray commands;
    for (const QVariant& value : service->slashCommands()) {
        const QVariantMap entry = value.toMap();
        const QString name =
            normalizedCommandName(entry.value(QStringLiteral("name")).toString());
        if (name.isEmpty()) continue;
        commands.append(QJsonObject{
            {QStringLiteral("name"), name},
            {QStringLiteral("templ"), entry.value(QStringLiteral("templ")).toString()},
            {QStringLiteral("note"), entry.value(QStringLiteral("note")).toString()}});
    }

    QJsonArray disabledTools;
    for (const QString& name : ToolRegistry::instance().disabledToolNames())
        disabledTools.append(name);

    const QJsonObject root{
        {QStringLiteral("format"), QString::fromLatin1(kFormat)},
        {QStringLiteral("version"), kVersion},
        {QStringLiteral("exportedAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("presets"), presets},
        {QStringLiteral("commands"), commands},
        {QStringLiteral("disabledTools"), disabledTools},
        {QStringLiteral("permissionMode"), service->defaultPermissionMode()}};

    QSaveFile file(localPath(path));
    if (!file.open(QIODevice::WriteOnly)) {
        setError(tr("无法创建预设包文件：%1").arg(file.errorString()));
        return false;
    }
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        setError(tr("预设包写入失败：%1").arg(file.errorString()));
        return false;
    }
    return true;
}

QVariantMap PresetPackTransfer::previewPack(const QString& path) {
    Pack pack;
    QVariantMap result = parsePack(path, &pack);
    if (!result.value(QStringLiteral("valid")).toBool()) return result;
    setError({});

    auto* service = SettingsService::instance();
    auto& registry = ToolRegistry::instance();

    QSet<QString> localPresets;
    for (const QVariant& value : service->promptPresets()) {
        const QString name =
            value.toMap().value(QStringLiteral("name")).toString().trimmed();
        if (!name.isEmpty()) localPresets.insert(name.toCaseFolded());
    }
    QSet<QString> localCommands;
    for (const QVariant& value : service->slashCommands()) {
        const QString name = normalizedCommandName(
            value.toMap().value(QStringLiteral("name")).toString());
        if (!name.isEmpty()) localCommands.insert(name);
    }

    QVariantList conflicts;
    int presetConflicts = 0;
    int commandConflicts = 0;
    int conflictsTotal = 0;
    for (const QJsonValue& value : pack.presets) {
        const QString name =
            value.toObject().value(QStringLiteral("name")).toString();
        if (!localPresets.contains(name.toCaseFolded())) continue;
        ++presetConflicts;
        ++conflictsTotal;
        if (conflicts.size() < kMaxPreviewConflicts)
            conflicts.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("preset")},
                                         {QStringLiteral("name"), name}});
    }
    for (const QJsonValue& value : pack.commands) {
        const QString name =
            value.toObject().value(QStringLiteral("name")).toString();
        if (!localCommands.contains(name)) continue;
        ++commandConflicts;
        ++conflictsTotal;
        if (conflicts.size() < kMaxPreviewConflicts)
            conflicts.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("command")},
                                         {QStringLiteral("name"), name}});
    }

    QStringList desiredTools;
    QSet<QString> desiredToolSet;
    for (const QString& name : pack.disabledTools) {
        if (!registry.isRegistered(name) || desiredToolSet.contains(name)) continue;
        desiredToolSet.insert(name);
        desiredTools.append(name);
    }
    const QStringList currentTools = registry.disabledToolNames();
    const QSet<QString> currentToolSet(currentTools.begin(), currentTools.end());
    const int unknownTools = pack.disabledTools.size() - desiredTools.size();

    const bool toolsChanged = !pack.disabledTools.isEmpty()
        && desiredToolSet != currentToolSet;
    const bool permissionChanged = !pack.permissionMode.isEmpty()
        && pack.permissionMode != service->defaultPermissionMode();

    QStringList warnings{
        tr("预设包不含模型配置、API Key 或 MCP 凭据。"),
        tr("导入会同时应用包内的禁用工具与默认权限模式，并立即生效（无需重启）。"),
        tr("会话模板、按项目覆盖与本机凭据不包含在预设包内。")};
    if (unknownTools > 0)
        warnings.append(tr("有 %1 个工具名未在本机注册，导入时会被忽略。")
                            .arg(unknownTools));
    if (!pack.disabledTools.isEmpty() && desiredTools.isEmpty())
        warnings.append(tr("包内禁用工具均未在本机注册，工具启用状态保持不变。"));

    return {{QStringLiteral("valid"), true},
            {QStringLiteral("error"), QString()},
            {QStringLiteral("presets"), pack.presets.size()},
            {QStringLiteral("commands"), pack.commands.size()},
            {QStringLiteral("disabledTools"), desiredTools.size()},
            {QStringLiteral("permissionMode"), pack.permissionMode},
            {QStringLiteral("currentPermissionMode"), service->defaultPermissionMode()},
            {QStringLiteral("conflicts"), conflicts},
            {QStringLiteral("conflictsTotal"), conflictsTotal},
            {QStringLiteral("changes"),
             QVariantMap{{QStringLiteral("presetsAdded"),
                          pack.presets.size() - presetConflicts},
                         {QStringLiteral("presetsOverwritten"), presetConflicts},
                         {QStringLiteral("commandsAdded"),
                          pack.commands.size() - commandConflicts},
                         {QStringLiteral("commandsOverwritten"), commandConflicts},
                         {QStringLiteral("toolsChanged"), toolsChanged},
                         {QStringLiteral("permissionChanged"), permissionChanged}}},
            {QStringLiteral("warnings"), warnings}};
}

bool PresetPackTransfer::importPack(const QString& path,
                                    const QString& conflictPolicy) {
    setError({});
    const QString policy = conflictPolicy.trimmed().toLower();
    if (policy != QLatin1String("overwrite") && policy != QLatin1String("skip")
        && policy != QLatin1String("rename")) {
        setError(tr("未知的冲突处理策略"));
        return false;
    }

    Pack pack;
    const QVariantMap parsed = parsePack(path, &pack);
    if (!parsed.value(QStringLiteral("valid")).toBool()) return false;

    auto* service = SettingsService::instance();
    auto& registry = ToolRegistry::instance();
    const QVariantList previousPresets = service->promptPresets();
    const QVariantList previousCommands = service->slashCommands();
    const QStringList previousTools = registry.disabledToolNames();
    const QString previousMode = service->defaultPermissionMode();

    QVariantList mergedPresets = previousPresets;
    QSet<QString> usedPresetNames;
    QHash<QString, int> presetIndex;
    for (int i = 0; i < mergedPresets.size(); ++i) {
        const QString name = mergedPresets.at(i)
                                 .toMap()
                                 .value(QStringLiteral("name"))
                                 .toString()
                                 .trimmed();
        if (name.isEmpty()) continue;
        usedPresetNames.insert(name.toCaseFolded());
        presetIndex.insert(name.toCaseFolded(), i);
    }
    for (const QJsonValue& value : pack.presets) {
        const QJsonObject entry = value.toObject();
        const QString name = entry.value(QStringLiteral("name")).toString().trimmed();
        const QString prompt = entry.value(QStringLiteral("prompt")).toString();
        const QString fold = name.toCaseFolded();
        const auto existing = presetIndex.constFind(fold);
        if (existing != presetIndex.constEnd()) {
            if (policy == QLatin1String("skip")) continue;
            if (policy == QLatin1String("overwrite")) {
                QVariantMap merged = mergedPresets.at(existing.value()).toMap();
                merged.insert(QStringLiteral("name"), name);
                merged.insert(QStringLiteral("prompt"), prompt);
                mergedPresets[existing.value()] = merged;
                continue;
            }
            const QString renamed = uniquePresetName(name, usedPresetNames);
            usedPresetNames.insert(renamed.toCaseFolded());
            mergedPresets.append(QVariantMap{{QStringLiteral("id"), newId()},
                                             {QStringLiteral("name"), renamed},
                                             {QStringLiteral("prompt"), prompt}});
            continue;
        }
        usedPresetNames.insert(fold);
        presetIndex.insert(fold, mergedPresets.size());
        mergedPresets.append(QVariantMap{{QStringLiteral("id"), newId()},
                                         {QStringLiteral("name"), name},
                                         {QStringLiteral("prompt"), prompt}});
    }

    QVariantList mergedCommands = previousCommands;
    QSet<QString> usedCommandNames;
    QHash<QString, int> commandIndex;
    for (int i = 0; i < mergedCommands.size(); ++i) {
        const QString name = normalizedCommandName(mergedCommands.at(i)
                                                       .toMap()
                                                       .value(QStringLiteral("name"))
                                                       .toString());
        if (name.isEmpty()) continue;
        usedCommandNames.insert(name);
        commandIndex.insert(name, i);
    }
    for (const QJsonValue& value : pack.commands) {
        const QJsonObject entry = value.toObject();
        const QString name = entry.value(QStringLiteral("name")).toString();
        const QString templ = entry.value(QStringLiteral("templ")).toString();
        const QString note = entry.value(QStringLiteral("note")).toString();
        const auto existing = commandIndex.constFind(name);
        if (existing != commandIndex.constEnd()) {
            if (policy == QLatin1String("skip")) continue;
            if (policy == QLatin1String("overwrite")) {
                QVariantMap merged = mergedCommands.at(existing.value()).toMap();
                merged.insert(QStringLiteral("name"), name);
                merged.insert(QStringLiteral("templ"), templ);
                merged.insert(QStringLiteral("note"), note);
                mergedCommands[existing.value()] = merged;
                continue;
            }
            const QString renamed = uniqueCommandName(name, usedCommandNames);
            usedCommandNames.insert(renamed);
            mergedCommands.append(QVariantMap{{QStringLiteral("id"), newId()},
                                              {QStringLiteral("name"), renamed},
                                              {QStringLiteral("templ"), templ},
                                              {QStringLiteral("note"), note}});
            continue;
        }
        usedCommandNames.insert(name);
        commandIndex.insert(name, mergedCommands.size());
        mergedCommands.append(QVariantMap{{QStringLiteral("id"), newId()},
                                          {QStringLiteral("name"), name},
                                          {QStringLiteral("templ"), templ},
                                          {QStringLiteral("note"), note}});
    }

    if (!service->applyPromptAssets(mergedPresets, mergedCommands)) {
        setError(tr("预设包中的预设或指令无效，导入已取消"));
        return false;
    }

    QStringList desiredTools;
    QSet<QString> desiredToolSet;
    for (const QString& name : pack.disabledTools) {
        if (!registry.isRegistered(name) || desiredToolSet.contains(name)) continue;
        desiredToolSet.insert(name);
        desiredTools.append(name);
    }
    // 包内只有本机不认识的名字时不改本机设置，避免旧/新版本包把工具全开。
    const bool applyTools = pack.disabledTools.isEmpty() || !desiredTools.isEmpty();
    if (applyTools && !registry.setDisabledTools(desiredTools)) {
        service->applyPromptAssets(previousPresets, previousCommands);
        setError(tr("工具启用状态写入失败，导入已回滚"));
        return false;
    }

    if (!pack.permissionMode.isEmpty()
        && service->defaultPermissionMode() != pack.permissionMode) {
        service->setDefaultPermissionMode(pack.permissionMode);
        if (service->defaultPermissionMode() != pack.permissionMode) {
            registry.setDisabledTools(previousTools);
            service->applyPromptAssets(previousPresets, previousCommands);
            setError(tr("默认权限模式写入失败，导入已回滚"));
            return false;
        }
    }

    setError({});
    return true;
}

} // namespace NexusAI
