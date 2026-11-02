#pragma once

#include <QList>
#include <QRegularExpression>
#include <QString>

namespace NexusAI {
namespace ModelOutput {

// docs/35: OpenAI-compatible endpoints only translate the model's native
// tool-call template into structured `tool_calls` when it arrives intact.
// The raw template leaks into `content` when:
//   * tools are deliberately withheld — the forced-final call after the step
//     or tool-execution budget is exhausted;
//   * the endpoint's parser fails, or the stream is cut mid-template.
//
// Treating it as text leaves the user with a pseudo-call instead of an
// answer, persisting it into history, and feeding it back into the next
// request teaches the model to repeat the pattern. Strip every
// `<tool_call>…</tool_call>` block (plus an unterminated block's tail) and
// report how many were removed.
inline int stripTextualToolCalls(QString& text) {
    if (text.isEmpty()
        || !text.contains(QLatin1String("<tool_call"), Qt::CaseInsensitive))
        return 0;

    static const QRegularExpression block(
        QStringLiteral("<tool_call\\b[\\s\\S]*?(?:</tool_call\\s*>|$)"),
        QRegularExpression::CaseInsensitiveOption);

    QList<QRegularExpressionMatch> matches;
    auto it = block.globalMatch(text);
    while (it.hasNext())
        matches.append(it.next());
    // Remove from the back so earlier captured offsets stay valid.
    for (auto match = matches.crbegin(); match != matches.crend(); ++match)
        text.remove(match->capturedStart(), match->capturedLength());
    text = text.trimmed();
    return static_cast<int>(matches.size());
}

} // namespace ModelOutput
} // namespace NexusAI