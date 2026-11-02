#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace NexusAI {

// docs/35 P4.4: shared by edit_file's confirmation preview and the structured
// proposal applier. Common-prefix/suffix line diff — O(n) and sufficient for
// the confirmation dialog (files are already size-capped by read/edit bounds).
// Returns empty when there are no changes.
inline QString unifiedDiff(const QString& oldText, const QString& newText,
                           const QString& oldLabel, const QString& newLabel,
                           int context = 3) {
    // Empty content is zero lines, not one empty line: otherwise a
    // new-file diff invents a spurious "-" line.
    const QStringList oldLines = oldText.isEmpty()
        ? QStringList() : oldText.split('\n', Qt::KeepEmptyParts);
    const QStringList newLines = newText.isEmpty()
        ? QStringList() : newText.split('\n', Qt::KeepEmptyParts);

    int p = 0;
    const int minLen = qMin(oldLines.size(), newLines.size());
    while (p < minLen && oldLines[p] == newLines[p]) ++p;

    int s = 0;
    while (s < minLen - p &&
           oldLines[oldLines.size() - 1 - s] == newLines[newLines.size() - 1 - s]) ++s;

    const int oldStart = p;
    const int oldEnd = oldLines.size() - s;
    const int newStart = p;
    const int newEnd = newLines.size() - s;

    if (oldStart == oldEnd && newStart == newEnd) return {};

    const int ctxBefore = qMin(context, oldStart);
    const int ctxAfter = qMin(context, static_cast<int>(oldLines.size()) - oldEnd);
    const int hunkOldStart = oldStart - ctxBefore;
    const int hunkNewStart = newStart - ctxBefore;
    const int hunkOldLen = ctxBefore + (oldEnd - oldStart) + ctxAfter;
    const int hunkNewLen = ctxBefore + (newEnd - newStart) + ctxAfter;

    QString diff;
    diff += QStringLiteral("--- %1\n").arg(oldLabel);
    diff += QStringLiteral("+++ %1\n").arg(newLabel);
    diff += QStringLiteral("@@ -%1,%2 +%3,%4 @@\n")
        .arg(hunkOldStart + 1).arg(hunkOldLen)
        .arg(hunkNewStart + 1).arg(hunkNewLen);

    for (int i = hunkOldStart; i < oldStart; ++i)
        diff += QStringLiteral(" %1\n").arg(oldLines[i]);
    for (int i = oldStart; i < oldEnd; ++i)
        diff += QStringLiteral("-%1\n").arg(oldLines[i]);
    for (int i = newStart; i < newEnd; ++i)
        diff += QStringLiteral("+%1\n").arg(newLines[i]);
    for (int i = oldEnd; i < oldEnd + ctxAfter; ++i)
        diff += QStringLiteral(" %1\n").arg(oldLines[i]);

    return diff;
}

// docs/34 P3.3: edit_file（以及 docs/35 P4.4 的 apply_proposal）锚点未命中
// 时的自动重定位建议。只做确定性探测（换行符差异 → 空白归一化后的唯一
// 位置 → 首行位置），给出行号让模型只重读局部；探测不出就不猜，保持原有
// 失败语义。返回值带前导空格，直接拼在 "old_string not found." 之后。
inline QString relocateHint(const QString& content, const QString& oldString) {
    // 1) 换行符差异：CRLF 文件 vs LF 锚点（常见于提案在 Linux 侧起草），
    // 以及反向。
    if (oldString.contains(QLatin1Char('\n'))
            && content.contains(QLatin1String("\r\n"))
            && !oldString.contains(QLatin1String("\r\n"))) {
        QString crlf = oldString;
        crlf.replace(QLatin1String("\n"), QLatin1String("\r\n"));
        if (content.contains(crlf))
            return QStringLiteral(
                " It matches this file with CRLF line endings - re-read the "
                "region and copy it verbatim.");
    }
    if (oldString.contains(QLatin1String("\r\n"))
            && !content.contains(QLatin1String("\r\n"))) {
        QString lf = oldString;
        lf.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        if (content.contains(lf))
            return QStringLiteral(
                " It matches this file with LF line endings - re-read the "
                "region and copy it verbatim.");
    }

    // 2) 空白归一化（逐行 trim + 折叠内部空白）后恰好命中一次：说明缩进
    // 或空格变了，内容本身还在。
    QStringList contentLines;
    for (const QString& line : content.split(QLatin1Char('\n')))
        contentLines.append(line.simplified());
    QStringList wantedLines;
    for (const QString& line : oldString.split(QLatin1Char('\n')))
        wantedLines.append(line.simplified());
    // 锚点两端的空行不参与定位（模型常带上来）。
    while (!wantedLines.isEmpty() && wantedLines.first().isEmpty())
        wantedLines.removeFirst();
    while (!wantedLines.isEmpty() && wantedLines.last().isEmpty())
        wantedLines.removeLast();
    if (wantedLines.isEmpty()) return {};

    QVector<int> blockHits;   // 1-based start lines
    for (int start = 0; start + wantedLines.size() <= contentLines.size(); ++start) {
        if (contentLines.at(start) != wantedLines.first()) continue;
        bool match = true;
        for (int k = 1; k < wantedLines.size(); ++k) {
            if (contentLines.at(start + k) != wantedLines.at(k)) {
                match = false;
                break;
            }
        }
        if (!match) continue;
        blockHits.append(start + 1);
        if (blockHits.size() > 1) break;   // 多义时不再声称「唯一」
    }
    if (blockHits.size() == 1) {
        return QStringLiteral(
            " It occurs once after collapsing whitespace/indentation "
            "(line %1) - re-read that region and copy the exact text.")
            .arg(blockHits.first());
    }

    // 3) 首行还在、上下文已变（常见于同一文件的先前提案改写了相邻代码）。
    // 太短的首行容易指向无关位置，宁可退回通用建议。
    const QString firstLine = wantedLines.first();
    if (firstLine.size() >= 8) {
        for (int i = 0; i < contentLines.size(); ++i) {
            if (contentLines.at(i) == firstLine) {
                return QStringLiteral(
                    " Its first line appears at line %1, but the surrounding "
                    "text has changed - re-read that region before retrying.")
                    .arg(i + 1);
            }
        }
    }
    return QStringLiteral(
        " Re-read the file (or the region named in the goal) before retrying "
        "- it may have changed since the anchor was drafted; do not guess.");
}

} // namespace NexusAI