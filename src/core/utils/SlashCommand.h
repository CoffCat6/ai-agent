#pragma once

#include <QString>
#include <QVector>

namespace NexusAI::SlashCommand {

// One user-defined slash command ("/review" → template). Kept in the same
// vocabulary as the settings UI: the template may carry a {text} placeholder
// that receives everything the user typed after the command word.
struct Command {
    QString name;      // without the leading '/', stored lowercased
    QString templ;     // expansion template, may contain {text}
};

// The argument after the command word, left-trimmed; empty for "/cmd".
inline QString argumentOf(const QString& input) {
    const int slash = input.indexOf(QLatin1Char('/'));
    int nameEnd = slash + 1;
    while (nameEnd < input.size() && !input.at(nameEnd).isSpace())
        ++nameEnd;
    int argStart = nameEnd;
    while (argStart < input.size() && input.at(argStart).isSpace())
        ++argStart;
    return input.mid(argStart);
}

// Expand a composer input through the user's command list. Pure and total:
//   - input not starting with '/' → returned unchanged;
//   - no command matches the first word → returned unchanged (the model can
//     still see "/etc/hosts" or "/usr/bin" style text);
//   - match: the template replaces the whole input. "{text}" placeholders
//     (all occurrences) receive the argument; a template without the
//     placeholder gets the argument appended after a blank line, so a bare
//     command word still carries typed text;
//   - an empty template falls back to returning the input unchanged.
// Case-insensitive on the command word; the argument keeps its case.
inline QString expand(const QString& input, const QVector<Command>& commands) {
    const QString trimmed = input.trimmed();
    if (!trimmed.startsWith(QLatin1Char('/')) || trimmed.length() < 2)
        return input;

    int nameEnd = 1;
    while (nameEnd < trimmed.size() && !trimmed.at(nameEnd).isSpace())
        ++nameEnd;
    const QString name = trimmed.mid(1, nameEnd - 1).toLower();

    const Command* hit = nullptr;
    for (const Command& c : commands) {
        if (c.name.compare(name, Qt::CaseInsensitive) == 0) {
            hit = &c;
            break;
        }
    }
    if (!hit || hit->templ.trimmed().isEmpty())
        return input;

    const QString argument = trimmed.mid(nameEnd).trimmed();
    if (!hit->templ.contains(QLatin1String("{text}")))
        return argument.isEmpty()
            ? hit->templ.trimmed()
            : hit->templ.trimmed() + QStringLiteral("\n\n") + argument;

    QString out = hit->templ;
    out.replace(QLatin1String("{text}"), argument);
    return out;
}

} // namespace NexusAI::SlashCommand
