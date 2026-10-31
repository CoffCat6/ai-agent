#include "ShortcutMap.h"

#include "PreferenceStore.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyCombination>
#include <QKeySequence>
#include <QLoggingCategory>
#include <QSet>

Q_LOGGING_CATEGORY(lcShortcutMap, "nexus.shortcuts")

namespace NexusAI {
namespace {

constexpr auto kShortcutKey = "ui.shortcuts";

} // namespace

ShortcutMap* ShortcutMap::s_instance = nullptr;

ShortcutMap* ShortcutMap::instance() {
    if (!s_instance) s_instance = new ShortcutMap(nullptr);
    return s_instance;
}

ShortcutMap* ShortcutMap::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

ShortcutMap::ShortcutMap(QObject* parent)
    : QObject(parent) {
    // 设置页的“恢复本页默认值”/全局复位都经 EditBuffer 写穿到
    // PreferenceStore；只有接上 saved 才能让 main.qml 的 Shortcut 跟着换。
    connect(PreferenceStore::instance(), &PreferenceStore::saved, this,
            [this](const QStringList& keys) {
                if (keys.contains(QString::fromLatin1(kShortcutKey))) reload();
            });
    reload();
}

const QVector<ShortcutMap::Action>& ShortcutMap::actionTable() {
    static const QVector<Action> actions = {
        {QStringLiteral("settings.open"),
         QCoreApplication::translate("NexusAI", "打开设置中心"), QString(),
         QStringLiteral("Ctrl+,")},
        {QStringLiteral("audit.open"),
         QCoreApplication::translate("NexusAI", "打开审计日志"), QString(),
         QStringLiteral("Ctrl+L")},
        {QStringLiteral("panel.toggle"),
         QCoreApplication::translate("NexusAI", "显示/隐藏右侧面板"), QString(),
         QStringLiteral("Ctrl+]")},
        {QStringLiteral("review.open"),
         QCoreApplication::translate("NexusAI", "打开文件改动审阅"), QString(),
         QStringLiteral("Ctrl+R")},
        {QStringLiteral("todo.open"),
         QCoreApplication::translate("NexusAI", "打开任务清单"), QString(),
         QStringLiteral("Ctrl+T")},
        {QStringLiteral("sidebar.toggle"),
         QCoreApplication::translate("NexusAI", "显示/隐藏侧边栏"), QString(),
         QStringLiteral("Ctrl+[")},
        {QStringLiteral("conversation.close"),
         QCoreApplication::translate("NexusAI", "结束当前对话"), QString(),
         QStringLiteral("Ctrl+W")},
        {QStringLiteral("conversation.next"),
         QCoreApplication::translate("NexusAI", "切换到下一个对话"), QString(),
         QStringLiteral("Ctrl+Tab")},
        {QStringLiteral("conversation.prev"),
         QCoreApplication::translate("NexusAI", "切换到上一个对话"), QString(),
         QStringLiteral("Ctrl+Shift+Tab")},
        {QStringLiteral("conversation.new"),
         QCoreApplication::translate("NexusAI", "新建会话"), QString(),
         QStringLiteral("Ctrl+N")},
        {QStringLiteral("conversation.search"),
         QCoreApplication::translate("NexusAI", "搜索会话"), QString(),
         QStringLiteral("Ctrl+K")},
        {QStringLiteral("theme.toggle"),
         QCoreApplication::translate("NexusAI", "切换深色/浅色主题"), QString(),
         QStringLiteral("Ctrl+D")},
        {QStringLiteral("shortcuts.open"),
         QCoreApplication::translate("NexusAI", "打开快捷键速查表"), QString(),
         QStringLiteral("Ctrl+/")},
    };
    return actions;
}

const ShortcutMap::Action* ShortcutMap::findAction(const QString& id) {
    for (const Action& action : actionTable())
        if (action.id == id) return &action;
    return nullptr;
}

QString ShortcutMap::normalizeSequence(const QString& sequence) {
    const QString trimmed = sequence.trimmed();
    if (trimmed.isEmpty()) return {};
    const QKeySequence parsed = QKeySequence::fromString(trimmed, QKeySequence::PortableText);
    if (parsed.isEmpty()) return {};
    return parsed.toString(QKeySequence::PortableText);
}

QString ShortcutMap::keyIdentifier(int key, int modifiers) {
    if (key <= 0) return {};
    // 纯修饰键不是可录制的组合键：按下 Ctrl 时不应把 Ctrl 自己录进去。
    switch (key) {
    case Qt::Key_Control:
    case Qt::Key_Shift:
    case Qt::Key_Alt:
    case Qt::Key_Meta:
    case Qt::Key_AltGr:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock:
        return {};
    default:
        break;
    }
    // KeyEvent 的修饰位里还混着 Keypad/GroupSwitch 等，QKeySequence 只认
    // 四个主修饰键。
    const Qt::KeyboardModifiers mods = Qt::KeyboardModifiers(modifiers)
        & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    return QKeySequence(QKeyCombination(mods, Qt::Key(key)))
        .toString(QKeySequence::PortableText);
}

QVariantMap ShortcutMap::bindings() const {
    QVariantMap map;
    for (const Action& action : actionTable())
        map.insert(action.id, m_effective.value(action.id));
    return map;
}

QVariantList ShortcutMap::actions() const {
    QVariantList list;
    list.reserve(actionTable().size());
    for (const Action& action : actionTable()) {
        const QString current = m_effective.value(action.id);
        const QString fallback = normalizeSequence(action.defaultSequence);
        list.append(QVariantMap{{QStringLiteral("id"), action.id},
                                {QStringLiteral("title"), action.title},
                                {QStringLiteral("description"), action.description},
                                {QStringLiteral("defaultSequence"), fallback},
                                {QStringLiteral("sequence"), current},
                                {QStringLiteral("modified"), current != fallback}});
    }
    return list;
}

int ShortcutMap::modifiedCount() const {
    return m_overrides.size();
}

bool ShortcutMap::recording() const {
    return m_recording;
}

void ShortcutMap::setRecording(bool recording) {
    if (m_recording == recording) return;
    m_recording = recording;
    emit recordingChanged();
}

QString ShortcutMap::sequenceFromKey(int key, int modifiers) const {
    return keyIdentifier(key, modifiers);
}

QVariantMap ShortcutMap::assign(const QString& actionId, const QString& sequence) {
    QVariantMap result{{QStringLiteral("ok"), false},
                       {QStringLiteral("error"), QString()},
                       {QStringLiteral("conflict"), QString()},
                       {QStringLiteral("sequence"), QString()}};

    const Action* action = findAction(actionId);
    if (!action) {
        result.insert(QStringLiteral("error"),
                      QCoreApplication::translate("NexusAI", "未知的快捷键动作"));
        return result;
    }

    const QString normalized = normalizeSequence(sequence);
    if (normalized.isEmpty()) {
        result.insert(QStringLiteral("error"),
                      QCoreApplication::translate("NexusAI", "无法识别的快捷键"));
        return result;
    }

    const QKeySequence candidate =
        QKeySequence::fromString(normalized, QKeySequence::PortableText);
    const QKeyCombination combo = candidate[0];
    const Qt::KeyboardModifiers mods = combo.keyboardModifiers();
    const Qt::Key key = combo.key();
    const bool hasCommand = mods.testFlag(Qt::ControlModifier)
        || mods.testFlag(Qt::AltModifier) || mods.testFlag(Qt::MetaModifier);
    const bool isFunctionKey = key >= Qt::Key_F1 && key <= Qt::Key_F35;
    // 裸字母/数字会被全局 Shortcut 截走，聊天输入框就再也打不出这个字符。
    if (!hasCommand && !isFunctionKey) {
        result.insert(QStringLiteral("error"),
                      QCoreApplication::translate(
                          "NexusAI", "全局快捷键需要包含 Ctrl、Alt 或 Meta，或使用功能键"));
        return result;
    }

    for (const Action& other : actionTable()) {
        if (other.id == actionId) continue;
        const QKeySequence otherSequence =
            QKeySequence::fromString(m_effective.value(other.id),
                                     QKeySequence::PortableText);
        if (!otherSequence.isEmpty() && otherSequence == candidate) {
            result.insert(QStringLiteral("conflict"), other.id);
            result.insert(QStringLiteral("error"),
                          QCoreApplication::translate("NexusAI", "该快捷键已被「%1」使用")
                              .arg(other.title));
            return result;
        }
    }

    QHash<QString, QString> next = m_overrides;
    if (normalized == normalizeSequence(action->defaultSequence))
        next.remove(actionId);
    else
        next.insert(actionId, normalized);

    if (!writeOverrides(next)) {
        result.insert(QStringLiteral("error"),
                      QCoreApplication::translate("NexusAI", "快捷键未能保存，请稍后重试"));
        return result;
    }
    // PreferenceStore::save 会同步发 saved → reload；reset 不发，统一在这里
    // 再读一次（reload 内部比较后无变化则不发 changed）。
    reload();
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("sequence"), m_effective.value(actionId));
    return result;
}

bool ShortcutMap::resetAction(const QString& actionId) {
    if (!m_overrides.contains(actionId)) return false;
    QHash<QString, QString> next = m_overrides;
    next.remove(actionId);
    if (!writeOverrides(next)) return false;
    reload();
    return true;
}

bool ShortcutMap::resetAll() {
    if (m_overrides.isEmpty()) return false;
    if (!writeOverrides({})) return false;
    reload();
    return true;
}

bool ShortcutMap::writeOverrides(const QHash<QString, QString>& overrides) {
    if (overrides.isEmpty())
        return PreferenceStore::instance()->reset(QString::fromLatin1(kShortcutKey));

    QJsonObject object;
    for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it)
        object.insert(it.key(), it.value());
    const QString json = QString::fromUtf8(
        QJsonDocument(object).toJson(QJsonDocument::Compact));
    return PreferenceStore::instance()->save(QString::fromLatin1(kShortcutKey), json);
}

void ShortcutMap::reload() {
    QHash<QString, QString> overrides;
    const QString stored =
        PreferenceStore::instance()->load(QString::fromLatin1(kShortcutKey)).toString();
    if (!stored.trimmed().isEmpty()) {
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(stored.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject object = doc.object();
            for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
                if (!it.value().isString()) continue;
                const QString normalized = normalizeSequence(it.value().toString());
                if (!normalized.isEmpty()) overrides.insert(it.key(), normalized);
            }
        } else {
            // 手改坏的 JSON 不该让全部快捷键失效：按“无自定义”处理。
            qCWarning(lcShortcutMap) << "ui.shortcuts is not a JSON object; using defaults";
        }
    }

    QHash<QString, QString> effective;
    QSet<QString> used;
    for (const Action& action : actionTable()) {
        QString sequence = overrides.value(action.id);
        if (!sequence.isEmpty() && used.contains(sequence)) {
            // 手工编辑留下的重复项：按动作表顺序先到先得，后来者回默认。
            qCWarning(lcShortcutMap) << "dropping duplicate shortcut for" << action.id
                                     << sequence;
            overrides.remove(action.id);
            sequence.clear();
        }
        if (sequence.isEmpty())
            sequence = normalizeSequence(action.defaultSequence);
        if (!used.contains(sequence)) used.insert(sequence);
        effective.insert(action.id, sequence);
    }

    const bool dirty = overrides != m_overrides || effective != m_effective;
    m_overrides = overrides;
    m_effective = effective;
    if (dirty) emit changed();
}

} // namespace NexusAI
