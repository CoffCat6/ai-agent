#include "ProposeEditTool.h"

#include "Sandbox.h"
#include "core/agent/SubAgentRunner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

namespace NexusAI {

namespace {

QJsonObject stringProp(const QString& description) {
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("string")},
                       {QStringLiteral("description"), description}};
}

// 项目根内的相对路径（报告用）；根不可知时退回绝对路径。
QString displayPath(const QString& absolute) {
    const QString root = Sandbox::currentProjectDir();
    if (root.isEmpty()) return absolute;
    const QString relative = QDir(root).relativeFilePath(absolute);
    return relative.startsWith(QLatin1String("..")) ? absolute : relative;
}

} // namespace

ToolDefinition ProposeEditTool::definition() const {
    ToolDefinition def;
    def.name = QLatin1String(kName);
    def.description = QStringLiteral(
        "Propose one or more anchored edits to an existing project file. This "
        "tool does NOT write anything: it validates the anchors against the "
        "file's current content and records a proposal for the primary agent, "
        "which applies it later with edit_file after the user approves. Use it "
        "when you are asked to draft a change. Each 'find' must occur exactly "
        "once in the file (0 or 2+ matches is rejected — add more surrounding "
        "context and retry). Paths are relative to the project root.");
    def.level = ToolLevel::Safe;
    def.readOnly = true;   // validates reads only; never touches the disk

    QJsonObject editProp{
        {"type", QStringLiteral("object")},
        {"properties",
         QJsonObject{{QStringLiteral("find"),
                      stringProp(QStringLiteral(
                          "Exact text from the file to replace; must occur "
                          "exactly once (add surrounding context if not)."))},
                     {QStringLiteral("replace"),
                      stringProp(QStringLiteral("Replacement text."))}}},
        {"required", QJsonArray{QStringLiteral("find"),
                                QStringLiteral("replace")}}};
    QJsonObject editsProp{
        {"type", QStringLiteral("array")},
        {"description", QStringLiteral(
            "Edits to this file, applied in order (anchors are validated "
            "against the content as it would look after the previous edits). "
            "At most %1 per call.").arg(kMaxEditsPerCall)},
        {"items", editProp},
        {"minItems", 1},
        {"maxItems", kMaxEditsPerCall}};

    QJsonObject properties;
    properties.insert(QStringLiteral("path"),
                      stringProp(QStringLiteral(
                          "File to change. Relative paths resolve against the "
                          "active project root; the file must already exist "
                          "and live inside the project.")));
    properties.insert(QStringLiteral("rationale"),
                      stringProp(QStringLiteral(
                          "One sentence: why this change is needed. Shown to "
                          "the user alongside the diff.")));
    properties.insert(QStringLiteral("edits"), editsProp);
    def.parameters = QJsonObject{
        {"type", QStringLiteral("object")},
        {"properties", properties},
        {"required", QJsonArray{QStringLiteral("path"),
                                QStringLiteral("rationale"),
                                QStringLiteral("edits")}}};
    return def;
}

ToolResult ProposeEditTool::execute(const QJsonObject& args) {
    QVector<SubAgentProposal>* sink = SubAgentProposalSink::current();
    if (!sink) {
        // 主代理（或任何非子代理调用方）误用/幻觉调用：提案只在子代理
        // 运行内有意义，主代理应当直接用 edit_file。
        return ToolResult::fail(QStringLiteral(
            "propose_edit is only available inside a sub-agent run; apply "
            "changes with edit_file/file_write instead."));
    }
    if (sink->size() >= kMaxProposalsPerRun) {
        return ToolResult::fail(QStringLiteral(
            "Proposal limit reached (%1 per run). Stop proposing, summarise "
            "what you would change, and report back so the primary agent can "
            "continue.").arg(kMaxProposalsPerRun));
    }

    const QString rawPath = args.value(QStringLiteral("path")).toString().trimmed();
    if (rawPath.isEmpty())
        return ToolResult::fail(QStringLiteral("'path' is required."));
    const QString rationale = args.value(QStringLiteral("rationale")).toString().trimmed();
    if (rationale.isEmpty())
        return ToolResult::fail(QStringLiteral(
            "'rationale' is required: state in one sentence why the change "
            "is needed."));

    const QString path = Sandbox::resolvePath(rawPath);
    Sandbox::DenyReason reason;
    if (!Sandbox::validatePath(path, /*isWrite=*/false, &reason))
        return ToolResult::fail(Sandbox::denialHint(reason));
    // 提案只针对工作区源码：没有活动项目时不做猜测（子代理常见于编码
    // 场景，路径必须能锚定到一个明确的根）。
    const QString root = Sandbox::currentProjectDir();
    if (root.isEmpty()) {
        return ToolResult::fail(QStringLiteral(
            "No active project: propose_edit needs an active project root to "
            "resolve the path against."));
    }
    const QString rootAbsolute = QDir::cleanPath(QFileInfo(root).absoluteFilePath());
    const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (absolute != rootAbsolute
            && !absolute.startsWith(rootAbsolute + QLatin1Char('/'))) {
        return ToolResult::fail(QStringLiteral(
            "Path escapes the project root: %1").arg(displayPath(path)));
    }

    QFile file(path);
    if (!file.exists())
        return ToolResult::fail(QStringLiteral(
            "File does not exist: %1 (propose_edit only edits existing "
            "files; new files are created by the primary agent).")
            .arg(displayPath(path)));
    if (file.size() > kMaxFileBytes) {
        return ToolResult::fail(QStringLiteral(
            "File is too large to propose edits on (%1 bytes; limit %2).")
            .arg(file.size()).arg(kMaxFileBytes));
    }
    if (!file.open(QIODevice::ReadOnly))
        return ToolResult::fail(QStringLiteral("Cannot open file: %1")
            .arg(file.errorString()));
    const QByteArray raw = file.readAll();
    file.close();
    // 二进制/非 UTF-8 文件不做锚点编辑：找不到可靠的行边界，主代理应用
    // 时也会二次解码出错。
    if (raw.contains('\0') || QString::fromUtf8(raw).contains(QChar::ReplacementCharacter)) {
        return ToolResult::fail(QStringLiteral(
            "File is binary or not valid UTF-8; propose_edit only handles "
            "UTF-8 text files."));
    }

    const QJsonArray rawEdits = args.value(QStringLiteral("edits")).toArray();
    if (rawEdits.isEmpty())
        return ToolResult::fail(QStringLiteral("'edits' must not be empty."));
    if (rawEdits.size() > kMaxEditsPerCall) {
        return ToolResult::fail(QStringLiteral(
            "Too many edits in one call (%1; limit %2). Split them across "
            "calls.").arg(rawEdits.size()).arg(kMaxEditsPerCall));
    }

    // 依次在「模拟内容」上校验锚点：同一调用里的多条编辑按顺序生效，
    // 后面的 find 必须能在前一条替换后的文本里唯一命中——否则主代理
    // 应用时会失败在半路。
    QString simulated = QString::fromUtf8(raw);
    SubAgentProposal proposal;
    proposal.path = absolute;
    proposal.rationale = rationale;
    for (int i = 0; i < rawEdits.size(); ++i) {
        const QJsonObject edit = rawEdits.at(i).toObject();
        const QString find = edit.value(QStringLiteral("find")).toString();
        const QString replace = edit.value(QStringLiteral("replace")).toString();
        if (find.isEmpty()) {
            return ToolResult::fail(QStringLiteral(
                "Edit %1: 'find' must not be empty (add the exact text to "
                "replace).").arg(i + 1));
        }
        if (find.size() > kMaxFindChars || replace.size() > kMaxReplaceChars) {
            return ToolResult::fail(QStringLiteral(
                "Edit %1 is too large (find %2 chars, limit %3; replace %4 "
                "chars, limit %5).").arg(i + 1).arg(find.size())
                .arg(kMaxFindChars).arg(replace.size()).arg(kMaxReplaceChars));
        }
        const int occurrences = simulated.count(find);
        if (occurrences == 0) {
            return ToolResult::fail(QStringLiteral(
                "Edit %1: 'find' does not occur in %2. Read the current file "
                "again and copy the exact text (whitespace matters).")
                .arg(i + 1).arg(displayPath(path)));
        }
        if (occurrences > 1) {
            return ToolResult::fail(QStringLiteral(
                "Edit %1: 'find' occurs %2 times in %3 — ambiguous anchor. "
                "Include more surrounding context so it matches exactly once.")
                .arg(i + 1).arg(occurrences).arg(displayPath(path)));
        }
        simulated.replace(find, replace);
        proposal.edits.append(SubAgentProposalEdit{find, replace});
    }

    sink->append(proposal);
    return ToolResult::ok(QStringLiteral(
        "Proposed %1 edit(s) to %2 (NOT applied — the primary agent will "
        "apply them after user approval). Do not assume the file has "
        "changed; base further reading on the current on-disk content.")
        .arg(proposal.edits.size()).arg(displayPath(path)));
}

} // namespace NexusAI
