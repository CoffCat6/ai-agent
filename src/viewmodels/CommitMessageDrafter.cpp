#include "CommitMessageDrafter.h"

#include "infrastructure/llm/IAIService.h"
#include "infrastructure/llm/LlmProtocolRegistry.h"
#include "viewmodels/SettingsService.h"

#include <QJSEngine>
#include <QJsonArray>
#include <QJsonObject>
#include <QQmlEngine>
#include <QRegularExpression>

#include <utility>

namespace NexusAI {

namespace {

QString systemInstruction() {
    return QStringLiteral(
        "你是提交说明生成器。根据用户给出的 git 已暂存差异，写一条提交说明。\n")
        + QStringLiteral("规则：\n")
        + QStringLiteral("1. 第一行是主题，使用 Conventional Commits 形式 "
                         "\"<type>(<scope>): <摘要>\"；type 只能取 feat/fix/docs/"
                         "style/refactor/test/perf/chore；主题不超过 50 个字符，"
                         "结尾不加句号。\n")
        + QStringLiteral("2. 只有主题说不清时才写正文：主题后空一行，正文每行不"
                         "超过 72 个字符，说明为什么这样改与影响，不要逐行复述差"
                         "异。\n")
        + QStringLiteral("3. 只输出提交说明本身：不要代码块围栏、不要引号、不要"
                         "前缀标签、不要额外解释。\n")
        + QStringLiteral("4. 用简体中文书写；文件路径、代码标识符与专有名词保持"
                         "原文。");
}

// Class-level seam; see the header for why it must not be per-instance.
std::function<DraftContext()> g_contextProvider;

} // namespace

CommitMessageDrafter* CommitMessageDrafter::s_instance = nullptr;

CommitMessageDrafter::CommitMessageDrafter(QObject* parent) : QObject(parent) {
    // Connected once: the timer is a member, so wiring it per request would
    // stack a new connection on every draft.
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this]() {
        if (m_done) return;
        if (m_service) {
            m_service->cancel();
            m_service->deleteLater();
        }
        finish(false, QString(),
              tr("起草超时（%1 秒）。").arg(kTimeoutMs / 1000));
    });
}

CommitMessageDrafter* CommitMessageDrafter::instance() {
    if (!s_instance) s_instance = new CommitMessageDrafter(nullptr);
    return s_instance;
}

CommitMessageDrafter* CommitMessageDrafter::create(QQmlEngine*, QJSEngine*) {
    QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
    return instance();
}

bool CommitMessageDrafter::blockedByLocalOnly(bool localOnlyMode,
                                              bool profileIsLocal) {
    return localOnlyMode && !profileIsLocal;
}

void CommitMessageDrafter::setContextProvider(
    std::function<DraftContext()> provider) {
    g_contextProvider = std::move(provider);
}

QString CommitMessageDrafter::truncateDiff(const QString& diff, int budget) {
    if (budget <= 0 || diff.size() <= budget) return diff;
    // Cutting mid-line would feed the model a fragment that looks like content.
    const int cut = diff.lastIndexOf(QLatin1Char('\n'), budget);
    const QString head = cut > 0 ? diff.left(cut) : diff.left(budget);
    return head + QStringLiteral("\n…[差异已截断，共 %1 字符]").arg(diff.size());
}

QString CommitMessageDrafter::buildPrompt(const QString& stagedDiff,
                                          const QStringList& recentSubjects) {
    QStringList parts;
    QStringList subjects;
    for (const QString& subject : recentSubjects) {
        const QString trimmed = subject.trimmed();
        if (!trimmed.isEmpty()) subjects.append(QStringLiteral("- ") + trimmed);
        if (subjects.size() >= 5) break;
    }
    if (!subjects.isEmpty()) {
        parts << QStringLiteral("参考最近提交主题（仅用于对齐风格）：")
              << subjects.join(QLatin1Char('\n'));
    }
    parts << QStringLiteral("已暂存差异：")
          << truncateDiff(stagedDiff, kDiffBudgetChars);
    return parts.join(QStringLiteral("\n"));
}

QString CommitMessageDrafter::cleanDraft(const QString& raw) {
    QString text = raw.trimmed();
    if (text.isEmpty()) return text;

    // Models wrap the answer in a fence even when told not to.
    static const QRegularExpression fenceRe(
        QStringLiteral("^```[a-zA-Z0-9_-]*[ \\t]*\\n([\\s\\S]*?)\\n?```$"));
    const QRegularExpressionMatch fenceMatch = fenceRe.match(text);
    if (fenceMatch.hasMatch())
        text = fenceMatch.captured(1).trimmed();

    if (text.size() >= 2
            && ((text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"')))
                || (text.startsWith(QChar(0x201c)) && text.endsWith(QChar(0x201d))))) {
        text = text.mid(1, text.size() - 2).trimmed();
    }

    const QStringList input = text.split(QLatin1Char('\n'));
    QStringList kept;
    int contentLines = 0;
    for (const QString& line : input) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            // Leading blanks are noise; interior blanks are paragraph breaks.
            if (!kept.isEmpty() && contentLines <= kMaxBodyLines)
                kept.append(QString());
            continue;
        }
        if (contentLines >= kMaxBodyLines) break;
        // Drop stray bullets or a "commit message:" label before the subject.
        QString value = trimmed;
        if (value.startsWith(QStringLiteral("- ")))
            value = value.mid(2).trimmed();
        else if (value.startsWith(QStringLiteral("* ")))
            value = value.mid(2).trimmed();
        if (kept.isEmpty()) {
            static const QRegularExpression labelRe(
                QStringLiteral("^(提交说明|commit message)\\s*[:：]\\s*"),
                QRegularExpression::CaseInsensitiveOption);
            value.remove(labelRe);
        }
        kept.append(value);
        ++contentLines;
    }

    // Collapse runs of blank lines and drop any at both ends.
    QStringList lines;
    for (const QString& line : kept) {
        if (line.isEmpty() && (lines.isEmpty() || lines.last().isEmpty()))
            continue;
        lines.append(line);
    }
    while (!lines.isEmpty() && lines.last().isEmpty())
        lines.removeLast();
    return lines.join(QLatin1Char('\n')).trimmed();
}

void CommitMessageDrafter::draft(const QString& stagedDiff,
                                 const QVariantList& recentSubjects) {
    if (!m_done) {
        emit drafted(false, QString(), tr("已有一次起草在进行中，请等待或先取消。"));
        return;
    }
    if (stagedDiff.trimmed().isEmpty()) {
        emit drafted(false, QString(), tr("没有已暂存的改动，无法起草。"));
        return;
    }

    DraftContext context;
    if (g_contextProvider) {
        context = g_contextProvider();
    } else {
        SettingsService* settings = SettingsService::instance();
        // B1-1：起草器要拿真实密钥发请求；QML 可见的 activeProfile()
        // 已脱敏，只剩 hasApiKey。
        context.profile = settings->activeProfileWithSecrets();
        context.localOnlyMode = settings->localOnlyMode();
    }
    const QVariantMap& profile = context.profile;
    const QString protocol = profile.value("apiProtocol").toString().trimmed();
    const QString baseUrl = profile.value("baseUrl").toString().trimmed();
    const QString model = profile.value("model").toString().trimmed();
    if (protocol.isEmpty() || baseUrl.isEmpty() || model.isEmpty()) {
        emit drafted(false, QString(),
                     tr("当前模型配置不完整（协议/地址/模型名），无法起草。"));
        return;
    }
    if (blockedByLocalOnly(context.localOnlyMode,
                           LlmProtocolRegistry::isLocal(
                               protocol, profile.value("provider").toString()))) {
        emit drafted(false, QString(),
                     tr("已开启仅本地模式：不会把改动内容发送给云端模型。"));
        return;
    }

    IAIService* service = LlmProtocolRegistry::createService(protocol, this);
    if (!service) {
        emit drafted(false, QString(), tr("未知的模型接口协议：%1").arg(protocol));
        return;
    }

    QStringList subjects;
    for (const QVariant& value : recentSubjects)
        subjects << value.toString();

    LlmProfileConfig cfg;
    cfg.baseUrl = baseUrl;
    cfg.apiKey = profile.value("apiKey").toString();
    cfg.model = model;
    service->configure(cfg);

    QJsonArray messages;
    QJsonObject system;
    system["role"] = QStringLiteral("system");
    system["content"] = systemInstruction();
    QJsonObject user;
    user["role"] = QStringLiteral("user");
    user["content"] = buildPrompt(stagedDiff, subjects);
    messages.append(system);
    messages.append(user);

    m_service = service;
    m_done = false;
    emit busyChanged();

    connect(service, &IAIService::responseReady, this,
            [this, service](const LLMResponse& response) {
        if (m_done) return;
        const QString text = cleanDraft(response.content);
        if (text.isEmpty())
            finish(false, QString(), tr("模型没有返回可用的提交说明。"));
        else
            finish(true, text, QString());
        service->deleteLater();
    });
    connect(service, &IAIService::errorOccurred, this,
            [this, service](const QString& error) {
        if (m_done) return;
        finish(false, QString(), error);
        service->deleteLater();
    });

    m_timeout.start(kTimeoutMs);
    service->chatComplete(messages);
}

void CommitMessageDrafter::cancel() {
    if (m_done) return;
    if (m_service) {
        m_service->cancel();
        m_service->deleteLater();
    }
    finish(false, QString(), tr("已取消起草。"));
}

void CommitMessageDrafter::finish(bool ok, const QString& text,
                                  const QString& error) {
    m_timeout.stop();
    m_service = nullptr;
    if (!m_done) {
        m_done = true;
        emit busyChanged();
    }
    emit drafted(ok, text, error);
}

} // namespace NexusAI
