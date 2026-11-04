#include "ApplyProposalTool.h"

#include "FileEditUtil.h"
#include "ProposalStore.h"
#include "Sandbox.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSaveFile>

namespace NexusAI {

namespace {

// 与 ToolRegistry 的文件工具同口径：读取上限与检查点捕获/摘要上限。
constexpr qint64 kMaxReadFileBytes = 8 * 1024 * 1024;
constexpr qint64 kMaxCheckpointCaptureBytes = 1024 * 1024;
constexpr qint64 kMaxCheckpointDiffInputBytes = 512 * 1024;
constexpr int kMaxCheckpointDiffChars = 64 * 1024;

// 只在 execute 成功写盘后调用（与 edit_file 的 finalizeCheckpoint 同语义）。
FileCheckpoint finalizeCheckpoint(FileCheckpoint checkpoint,
                                  const QByteArray& afterBytes) {
    checkpoint.afterHash = QString::fromLatin1(
        QCryptographicHash::hash(afterBytes, QCryptographicHash::Sha256).toHex());
    if (checkpoint.truncated) {
        checkpoint.diffTruncated = true;
        return checkpoint;
    }
    const QByteArray beforeBytes =
        checkpoint.existed ? checkpoint.beforeContent : QByteArray();
    if (beforeBytes.size() > kMaxCheckpointDiffInputBytes
            || afterBytes.size() > kMaxCheckpointDiffInputBytes) {
        checkpoint.diffTruncated = true;
        return checkpoint;
    }
    const QString beforeText = checkpoint.existed
        ? QString::fromUtf8(beforeBytes) : QString();
    const QString afterText = QString::fromUtf8(afterBytes);
    if (beforeText.contains(QChar::ReplacementCharacter)
            || afterText.contains(QChar::ReplacementCharacter)) {
        checkpoint.diffTruncated = true;
        return checkpoint;
    }
    QString diff = unifiedDiff(
        beforeText, afterText,
        checkpoint.existed ? checkpoint.path : QStringLiteral("/dev/null"),
        checkpoint.path);
    for (const QString& line : diff.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1String("+++"))
                || line.startsWith(QLatin1String("---")))
            continue;
        if (line.startsWith(QLatin1Char('+'))) ++checkpoint.addedLines;
        else if (line.startsWith(QLatin1Char('-'))) ++checkpoint.removedLines;
    }
    if (diff.size() > kMaxCheckpointDiffChars) {
        int cut = diff.lastIndexOf(QLatin1Char('\n'), kMaxCheckpointDiffChars);
        if (cut <= 0) cut = kMaxCheckpointDiffChars;
        diff = diff.left(cut) + QStringLiteral("\n…[diff truncated]\n");
        checkpoint.diffTruncated = true;
    }
    checkpoint.diff = diff;
    return checkpoint;
}

// 读取当前文件并按顺序模拟提案里的所有编辑（同一条提案内的后续锚点必须
// 能命中前一条替换后的文本）。失败时把确定性重定位提示拼进 error。
bool readAndSimulate(const StoredProposal& proposal,
                     QString* before, QString* after, QString* error) {
    QFile file(proposal.path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Cannot open file for reading: %1")
                     .arg(file.errorString());
        return false;
    }
    if (file.size() > kMaxReadFileBytes) {
        *error = QStringLiteral("File too large to apply the proposal (%1 bytes; "
                                "limit %2).")
                     .arg(file.size()).arg(kMaxReadFileBytes);
        return false;
    }
    const QString content = QString::fromUtf8(file.readAll());
    file.close();
    if (content.contains(QChar::ReplacementCharacter)) {
        *error = QStringLiteral("File is not valid UTF-8 text.");
        return false;
    }

    QString simulated = content;
    for (int i = 0; i < proposal.edits.size(); ++i) {
        const SubAgentProposalEdit& edit = proposal.edits.at(i);
        const int occurrences = simulated.count(edit.find);
        if (occurrences == 0) {
            *error = QStringLiteral(
                "Edit %1: the proposal anchor no longer occurs in %2.")
                .arg(i + 1).arg(proposal.path)
                + relocateHint(simulated, edit.find);
            return false;
        }
        if (occurrences > 1) {
            *error = QStringLiteral(
                "Edit %1: the proposal anchor occurs %2 times now — applying it "
                "would be ambiguous. Re-read the file and re-propose.")
                .arg(i + 1).arg(occurrences);
            return false;
        }
        simulated.replace(edit.find, edit.replace);
    }
    if (before) *before = content;
    if (after) *after = simulated;
    return true;
}

QString unknownId() {
    return QStringLiteral(
        "Unknown or expired proposal id. Use one of the ids listed under "
        "'Proposal ids' in the sub-agent report; ids are in-memory only and "
        "do not survive an app restart.");
}

} // namespace

ToolDefinition ApplyProposalTool::definition() const {
    ToolDefinition def;
    def.name = QLatin1String(kName);
    def.description = QStringLiteral(
        "Apply a change proposal produced by a spawn_agent sub-agent (coder "
        "role). Pass the proposal id shown under 'Proposal ids' in the "
        "sub-agent report. The tool re-reads the file, re-validates the "
        "anchors against its current content, and writes only after the user "
        "approves the diff. Prefer this over copying 'find' text into "
        "edit_file; use edit_file/file_write only when the proposal id is not "
        "listed or the anchor needs changing. Each call applies exactly one "
        "proposal; proposals for the same file are applied in the order "
        "listed, re-checking the remaining anchors after each edit.");
    def.level = ToolLevel::RequiresConfirm;
    def.autoEditSafe = true;   // 与会话自动编辑模式兼容；确认仍由策略决定
    def.readOnly = false;
    QJsonObject props;
    QJsonObject idParam;
    idParam["type"] = QLatin1String("string");
    idParam["description"] = QStringLiteral(
        "Proposal id copied from the sub-agent report's 'Proposal ids' line "
        "(shape: <spawn-node-id>#<n>).");
    props["id"] = idParam;
    def.parameters = QJsonObject{
        {"type", QLatin1String("object")},
        {"properties", props},
        {"required", QJsonArray{QStringLiteral("id")}}};
    return def;
}

QString ApplyProposalTool::generatePreview(const QJsonObject& args) const {
    const std::shared_ptr<ProposalStore> store = ProposalStoreRouter::current();
    if (!store) return unknownId();
    const QString id = args.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty()) return QStringLiteral("Proposal id is required.");
    StoredProposal proposal;
    if (!store->get(id, &proposal)) return unknownId();
    if (proposal.applied)
        return QStringLiteral("Proposal %1 was already applied.").arg(id);
    QString before;
    QString after;
    QString error;
    if (!readAndSimulate(proposal, &before, &after, &error)) return error;
    return unifiedDiff(before, after, proposal.path, proposal.path);
}

ToolResult ApplyProposalTool::execute(const QJsonObject& args) {
    const std::shared_ptr<ProposalStore> store = ProposalStoreRouter::current();
    if (!store)
        return ToolResult::fail(QStringLiteral(
            "apply_proposal is only available inside an agent session."));
    const QString id = args.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty())
        return ToolResult::fail(QStringLiteral(
            "Proposal id is required (copy it from the sub-agent report's "
            "'Proposal ids' line)."));

    StoredProposal proposal;
    if (!store->get(id, &proposal)) return ToolResult::fail(unknownId());
    if (proposal.applied)
        return ToolResult::fail(QStringLiteral(
            "Proposal %1 was already applied; do not apply it twice.")
            .arg(id));

    Sandbox::DenyReason reason;
    if (!Sandbox::validatePath(proposal.path, true, &reason))
        return ToolResult::fail(Sandbox::denialHint(reason));

    // docs/36 B2-1: 提案应用同样是「读当前文件 -> 模拟编辑 -> 覆盖」，必须
    // 与同路径的 write_file/edit_file/其它提案串行。进入临界区后重新取一次
    // 提案：并发调用可能已经消费掉它（applied），此时不能基于旧快照再写一遍。
    const PathWriteGuard writeGuard(proposal.path);
    if (!store->get(id, &proposal)) return ToolResult::fail(unknownId());
    if (proposal.applied)
        return ToolResult::fail(QStringLiteral(
            "Proposal %1 was already applied; do not apply it twice.")
            .arg(id));

    QString content;
    QString updated;
    QString error;
    if (!readAndSimulate(proposal, &content, &updated, &error))
        return ToolResult::fail(error);

    const QByteArray bytes = updated.toUtf8();
    FileCheckpoint checkpoint;
    checkpoint.path = proposal.path;
    checkpoint.existed = true;
    const QByteArray beforeBytes = content.toUtf8();
    if (beforeBytes.size() > kMaxCheckpointCaptureBytes)
        checkpoint.truncated = true;
    else
        checkpoint.beforeContent = beforeBytes;

    QSaveFile out(proposal.path);
    if (!out.open(QIODevice::WriteOnly))
        return ToolResult::fail(QStringLiteral("Cannot open file for writing: %1")
                                    .arg(out.errorString()));
    if (out.write(bytes) != bytes.size()) {
        out.cancelWriting();
        return ToolResult::fail(QStringLiteral("Short write (disk full/quota?): %1")
                                    .arg(out.errorString()));
    }
    if (!out.commit())
        return ToolResult::fail(QStringLiteral(
            "Failed to commit atomic write: %1").arg(out.errorString()));

    store->markApplied(id);
    ToolResult result = ToolResult::ok(QStringLiteral(
        "Applied proposal %1: %2 edit(s) to %3 (file now %4 bytes). This "
        "proposal is now consumed; other proposals still need their own "
        "apply_proposal call and user approval.")
        .arg(id).arg(proposal.edits.size()).arg(proposal.path)
        .arg(bytes.size()));
    result.checkpoint = finalizeCheckpoint(checkpoint, bytes);
    return result;
}

} // namespace NexusAI