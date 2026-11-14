#pragma once

#include <QChar>
#include <QHash>
#include <QString>

namespace NexusAI {
namespace MathText {

// docs/53: lightweight LaTeX → Unicode math approximation for the chat
// render path (NO WebEngine — see docs/53 §1 for the trade-off). Coverage is
// deliberately a safe subset:
//   - $$…$$ / \[…\] display spans and $…$ / \(…\) inline spans are converted;
//     a "$5 and $10" span WITHOUT math markers is left untouched;
//   - supported: greek letters, common operators/relations/arrows, ^ and _
//     scripts (digit/letter mappings; unmapped chars keep the ^ marker),
//     \frac, \sqrt, \text/\mathrm/\mathbf de-shelling, \left/\right stripping,
//     & column separators, \\ line breaks, \begin{…}/\end{…} shell removal;
//   - unknown \commands are kept verbatim (never silently mangled);
//   - inline `code` spans are skipped entirely (prices like `x = $5` stay).
// The output is plain text — the goal is READABILITY, not typesetting.

namespace {

// Single-char command map (\alpha → α, \times → ×, …).
inline const QHash<QString, QString>& symbolMap() {
    static const QHash<QString, QString> m = {
        {QStringLiteral("alpha"), QStringLiteral("α")}, {QStringLiteral("beta"), QStringLiteral("β")},
        {QStringLiteral("gamma"), QStringLiteral("γ")}, {QStringLiteral("Gamma"), QStringLiteral("Γ")},
        {QStringLiteral("delta"), QStringLiteral("δ")}, {QStringLiteral("Delta"), QStringLiteral("Δ")},
        {QStringLiteral("epsilon"), QStringLiteral("ε")}, {QStringLiteral("varepsilon"), QStringLiteral("ε")},
        {QStringLiteral("zeta"), QStringLiteral("ζ")}, {QStringLiteral("eta"), QStringLiteral("η")},
        {QStringLiteral("theta"), QStringLiteral("θ")}, {QStringLiteral("Theta"), QStringLiteral("Θ")},
        {QStringLiteral("iota"), QStringLiteral("ι")}, {QStringLiteral("kappa"), QStringLiteral("κ")},
        {QStringLiteral("lambda"), QStringLiteral("λ")}, {QStringLiteral("Lambda"), QStringLiteral("Λ")},
        {QStringLiteral("mu"), QStringLiteral("μ")}, {QStringLiteral("nu"), QStringLiteral("ν")},
        {QStringLiteral("xi"), QStringLiteral("ξ")}, {QStringLiteral("Xi"), QStringLiteral("Ξ")},
        {QStringLiteral("pi"), QStringLiteral("π")}, {QStringLiteral("Pi"), QStringLiteral("Π")},
        {QStringLiteral("rho"), QStringLiteral("ρ")}, {QStringLiteral("sigma"), QStringLiteral("σ")},
        {QStringLiteral("Sigma"), QStringLiteral("Σ")}, {QStringLiteral("tau"), QStringLiteral("τ")},
        {QStringLiteral("phi"), QStringLiteral("φ")}, {QStringLiteral("Phi"), QStringLiteral("Φ")},
        {QStringLiteral("varphi"), QStringLiteral("φ")}, {QStringLiteral("chi"), QStringLiteral("χ")},
        {QStringLiteral("psi"), QStringLiteral("ψ")}, {QStringLiteral("Psi"), QStringLiteral("Ψ")},
        {QStringLiteral("omega"), QStringLiteral("ω")}, {QStringLiteral("Omega"), QStringLiteral("Ω")},
        {QStringLiteral("times"), QStringLiteral("×")}, {QStringLiteral("cdot"), QStringLiteral("·")},
        {QStringLiteral("div"), QStringLiteral("÷")}, {QStringLiteral("pm"), QStringLiteral("±")},
        {QStringLiteral("mp"), QStringLiteral("∓")}, {QStringLiteral("leq"), QStringLiteral("≤")},
        {QStringLiteral("le"), QStringLiteral("≤")}, {QStringLiteral("geq"), QStringLiteral("≥")},
        {QStringLiteral("ge"), QStringLiteral("≥")}, {QStringLiteral("neq"), QStringLiteral("≠")},
        {QStringLiteral("ne"), QStringLiteral("≠")}, {QStringLiteral("approx"), QStringLiteral("≈")},
        {QStringLiteral("equiv"), QStringLiteral("≡")}, {QStringLiteral("sim"), QStringLiteral("∼")},
        {QStringLiteral("infty"), QStringLiteral("∞")}, {QStringLiteral("sum"), QStringLiteral("∑")},
        {QStringLiteral("prod"), QStringLiteral("∏")}, {QStringLiteral("int"), QStringLiteral("∫")},
        {QStringLiteral("partial"), QStringLiteral("∂")}, {QStringLiteral("nabla"), QStringLiteral("∇")},
        {QStringLiteral("in"), QStringLiteral("∈")}, {QStringLiteral("notin"), QStringLiteral("∉")},
        {QStringLiteral("subset"), QStringLiteral("⊂")}, {QStringLiteral("subseteq"), QStringLiteral("⊆")},
        {QStringLiteral("supset"), QStringLiteral("⊃")}, {QStringLiteral("supseteq"), QStringLiteral("⊇")},
        {QStringLiteral("cup"), QStringLiteral("∪")}, {QStringLiteral("cap"), QStringLiteral("∩")},
        {QStringLiteral("forall"), QStringLiteral("∀")}, {QStringLiteral("exists"), QStringLiteral("∃")},
        {QStringLiteral("rightarrow"), QStringLiteral("→")}, {QStringLiteral("to"), QStringLiteral("→")},
        {QStringLiteral("leftarrow"), QStringLiteral("←")}, {QStringLiteral("Rightarrow"), QStringLiteral("⇒")},
        {QStringLiteral("Leftarrow"), QStringLiteral("⇐")}, {QStringLiteral("leftrightarrow"), QStringLiteral("↔")},
        {QStringLiteral("Leftrightarrow"), QStringLiteral("⇔")}, {QStringLiteral("mapsto"), QStringLiteral("↦")},
        {QStringLiteral("ldots"), QStringLiteral("…")}, {QStringLiteral("dots"), QStringLiteral("…")},
        {QStringLiteral("cdots"), QStringLiteral("⋯")}, {QStringLiteral("angle"), QStringLiteral("∠")},
        {QStringLiteral("degree"), QStringLiteral("°")}, {QStringLiteral("circ"), QStringLiteral("°")},
        {QStringLiteral("neg"), QStringLiteral("¬")}, {QStringLiteral("land"), QStringLiteral("∧")},
        {QStringLiteral("lor"), QStringLiteral("∨")}, {QStringLiteral("oplus"), QStringLiteral("⊕")},
        {QStringLiteral("otimes"), QStringLiteral("⊗")}, {QStringLiteral("perp"), QStringLiteral("⊥")},
        {QStringLiteral("propto"), QStringLiteral("∝")}, {QStringLiteral("ell"), QStringLiteral("ℓ")},
        {QStringLiteral("hbar"), QStringLiteral("ℏ")}, {QStringLiteral("emptyset"), QStringLiteral("∅")},
        {QStringLiteral("varnothing"), QStringLiteral("∅")}, {QStringLiteral("quad"), QStringLiteral("  ")},
        {QStringLiteral("qquad"), QStringLiteral("    ")}, {QStringLiteral("lfloor"), QStringLiteral("⌊")},
        {QStringLiteral("rfloor"), QStringLiteral("⌋")}, {QStringLiteral("lceil"), QStringLiteral("⌈")},
        {QStringLiteral("rceil"), QStringLiteral("⌉")},
    };
    return m;
}

// Digit/letter → superscript char, '\0' when unmappable.
inline QChar toSuperscript(QChar c) {
    static const QString digits = QStringLiteral("0123456789");
    static const QString supDigits = QStringLiteral("⁰¹²³⁴⁵⁶⁷⁸⁹");
    static const QString lower = QStringLiteral("abcdefghijklmnopqrstuvwxyz");
    static const QString supLower = QStringLiteral("ᵃᵇᶜᵈᵉᶠᵍʰⁱʲᵏˡᵐⁿᵒᵖqʳˢᵗᵘᵛʷˣʸᶻ");
    static const QString upper = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    static const QString supUpper = QStringLiteral("ᴬᴮᶜᴰᴱᶠᴳᴴᴵᴶᴷᴸᴹᴺᴼᴾQᴿˢᵀᵁⱽᵂᴳᴴᴵᴶᴷ");
    static const QString extra = QStringLiteral("+-=()n");
    static const QString supExtra = QStringLiteral("⁺⁻⁼⁽⁾ⁿ");
    const int d = digits.indexOf(c);
    if (d >= 0) return supDigits.at(d);
    const int l = lower.indexOf(c);
    if (l >= 0) return supLower.at(l);
    const int u = upper.indexOf(c);
    if (u >= 0) return supUpper.at(u);
    const int e = extra.indexOf(c);
    if (e >= 0) return supExtra.at(e);
    return QChar();
}

inline QChar toSubscript(QChar c) {
    static const QString digits = QStringLiteral("0123456789");
    static const QString subDigits = QStringLiteral("₀₁₂₃₄₅₆₇₈₉");
    static const QString lower = QStringLiteral("aeioklmnprstuvx");
    static const QString subLower = QStringLiteral("ₐₑᵢₒₖₗₘₙₚᵣₛₜᵤᵥₓ");
    static const QString extra = QStringLiteral("+-=()");
    static const QString subExtra = QStringLiteral("₊₋₌₍₎");
    const int d = digits.indexOf(c);
    if (d >= 0) return subDigits.at(d);
    const int l = lower.indexOf(c);
    if (l >= 0) return subLower.at(l);
    const int e = extra.indexOf(c);
    if (e >= 0) return subExtra.at(e);
    return QChar();
}

inline bool isAlphaAscii(QChar c) {
    return (c >= QLatin1Char('a') && c <= QLatin1Char('z'))
        || (c >= QLatin1Char('A') && c <= QLatin1Char('Z'));
}

// Reads a {…} group starting at input[i] == '{' (i is advanced PAST the
// closing '}'). Returns the inner text; unmatched → empty and i stays.
inline QString readBraceGroup(const QString& s, int& i) {
    if (i >= s.size() || s.at(i) != QLatin1Char('{')) return QString();
    int depth = 0;
    const int start = ++i;
    for (; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('{')) ++depth;
        else if (c == QLatin1Char('}')) {
            if (depth == 0) {
                const QString inner = s.mid(start, i - start);
                ++i;   // step past '}'
                return inner;
            }
            --depth;
        }
    }
    return QString();
}

// Core fragment conversion (the text INSIDE a math span). Recursion depth is
// bounded — nested \frac/\text groups deeper than 8 fall back to verbatim.
inline QString convertFragment(const QString& tex, int depth) {
    if (depth > 8) return tex;
    QString out;
    int i = 0;
    while (i < tex.size()) {
        const QChar c = tex.at(i);
        if (c == QLatin1Char('\\')) {
            int j = i + 1;
            if (j >= tex.size()) { out += c; break; }
            // \, \; \! \  (spacing) → a single space; \\ → line break.
            if (!isAlphaAscii(tex.at(j))) {
                const QChar p = tex.at(j);
                if (p == QLatin1Char('\\')) {
                    out += QLatin1Char('\n');
                    i = j + 1;
                    continue;
                }
                if (p == QLatin1Char(',') || p == QLatin1Char(';')
                        || p == QLatin1Char('!') || p == QLatin1Char(' ')
                        || p == QLatin1Char(':')) {
                    out += QLatin1Char(' ');
                    i = j + 1;
                    continue;
                }
                // \( \) \[ \] inline markers inside a fragment: drop the shell.
                if ((p == QLatin1Char('(') || p == QLatin1Char('['))
                        && i + 2 < tex.size()) {
                    // handled by caller normally; keep verbatim otherwise
                }
                out += c;
                ++i;
                continue;
            }
            QString cmd;
            while (j < tex.size() && isAlphaAscii(tex.at(j))) cmd += tex.at(j++);
            if (cmd == QLatin1String("frac") || cmd == QLatin1String("dfrac")
                    || cmd == QLatin1String("tfrac")) {
                const QString num = readBraceGroup(tex, j);
                const QString den = readBraceGroup(tex, j);
                if (!num.isNull() && !den.isNull()) {
                    out += QLatin1Char('(') + convertFragment(num, depth + 1)
                           + QLatin1String(")/(") + convertFragment(den, depth + 1)
                           + QLatin1Char(')');
                    i = j;
                    continue;
                }
                out += QLatin1Char('\\') + cmd;
                i = j;
                continue;
            }
            if (cmd == QLatin1String("sqrt")) {
                // \sqrt[n]{x} → ⁿ√(x); \sqrt{x} → √(x)
                QString index;
                if (j < tex.size() && tex.at(j) == QLatin1Char('[')) {
                    const int close = tex.indexOf(QLatin1Char(']'), j);
                    if (close > j) {
                        index = tex.mid(j + 1, close - j - 1);
                        j = close + 1;
                    }
                }
                const QString body = readBraceGroup(tex, j);
                if (!body.isNull()) {
                    if (!index.isEmpty()) {
                        for (const QChar ic : index) {
                            const QChar sup = toSuperscript(ic);
                            out += sup.isNull() ? ic : sup;
                        }
                    }
                    // NOT QLatin1String: "√" is non-Latin-1 and would be
                    // mangled into a UTF-8 byte sequence.
                    out += QStringLiteral("√(") + convertFragment(body, depth + 1)
                           + QLatin1Char(')');
                    i = j;
                    continue;
                }
                out += QLatin1Char('\\') + cmd;
                i = j;
                continue;
            }
            if (cmd == QLatin1String("text") || cmd == QLatin1String("mathrm")
                    || cmd == QLatin1String("mathbf") || cmd == QLatin1String("mathit")
                    || cmd == QLatin1String("textrm") || cmd == QLatin1String("operatorname")) {
                const QString body = readBraceGroup(tex, j);
                if (!body.isNull()) {
                    out += convertFragment(body, depth + 1);
                    i = j;
                    continue;
                }
            }
            if (cmd == QLatin1String("left") || cmd == QLatin1String("right")
                    || cmd == QLatin1String("displaystyle")) {
                i = j;   // shell commands: drop, keep the delimiter itself
                continue;
            }
            if (cmd == QLatin1String("begin") || cmd == QLatin1String("end")) {
                const QString env = readBraceGroup(tex, j);
                Q_UNUSED(env);   // shell removal; the content flows through
                i = j;
                continue;
            }
            const auto it = symbolMap().constFind(cmd);
            if (it != symbolMap().constEnd()) {
                out += it.value();
                i = j;
                continue;
            }
            // Unknown command: keep verbatim (never silently mangle).
            out += QLatin1Char('\\') + cmd;
            i = j;
            continue;
        }
        if (c == QLatin1Char('^') || c == QLatin1Char('_')) {
            const bool sup = (c == QLatin1Char('^'));
            ++i;
            if (i >= tex.size()) { out += c; continue; }
            QString group;
            if (tex.at(i) == QLatin1Char('{')) {
                group = readBraceGroup(tex, i);
                if (group.isNull()) { out += c; continue; }
                // Commands inside the group convert FIRST (^\alpha → ^α);
                // the char-by-char mapping below then does what it can.
                group = convertFragment(group, depth + 1);
            } else {
                group = QString(tex.at(i));
                ++i;
            }
            QString mapped;
            bool allMapped = !group.isEmpty();
            for (const QChar gc : group) {
                const QChar m = sup ? toSuperscript(gc) : toSubscript(gc);
                if (m.isNull()) { allMapped = false; break; }
                mapped += m;
            }
            if (allMapped) out += mapped;
            else out += c + group;   // unmappable: keep the TeX form verbatim
            continue;
        }
        if (c == QLatin1Char('&')) { out += QStringLiteral("  "); ++i; continue; }
        out += c;
        ++i;
    }
    return out;
}

// Does the span LOOK like math? Guards "the price is $5 and $10" from being
// mangled — a span needs at least one math marker (a command, a script, a
// group, or an equation sign).
inline bool looksLikeMath(const QString& span) {
    if (span.contains(QLatin1Char('\\'))) return true;
    if (span.contains(QLatin1Char('^')) || span.contains(QLatin1Char('_'))) return true;
    if (span.contains(QLatin1Char('{')) && span.contains(QLatin1Char('}'))) return true;
    if (span.contains(QLatin1Char('='))) return true;
    return false;
}

} // namespace

inline QString render(const QString& input) {
    if (input.isEmpty()) return input;
    QString out;
    out.reserve(input.size());
    int i = 0;
    bool inBacktick = false;
    while (i < input.size()) {
        const QChar c = input.at(i);
        // Inline `code` spans are passed through untouched.
        if (c == QLatin1Char('`')) {
            inBacktick = !inBacktick;
            out += c;
            ++i;
            continue;
        }
        if (!inBacktick && c == QLatin1Char('$') && i + 1 < input.size()
                && input.at(i + 1) == QLatin1Char('$')) {
            const int end = input.indexOf(QStringLiteral("$$"), i + 2);
            if (end > i) {
                const QString tex = input.mid(i + 2, end - i - 2);
                out += QLatin1Char('\n') + convertFragment(tex, 0)
                       + QLatin1Char('\n');
                i = end + 2;
                continue;
            }
        }
        if (!inBacktick && c == QLatin1Char('\\') && i + 1 < input.size()) {
            const QChar n = input.at(i + 1);
            if (n == QLatin1Char('[')) {
                const int end = input.indexOf(QStringLiteral("\\]"), i + 2);
                if (end > i) {
                    out += QLatin1Char('\n')
                           + convertFragment(input.mid(i + 2, end - i - 2), 0)
                           + QLatin1Char('\n');
                    i = end + 2;
                    continue;
                }
            }
            if (n == QLatin1Char('(')) {
                const int end = input.indexOf(QStringLiteral("\\)"), i + 2);
                if (end > i) {
                    out += convertFragment(input.mid(i + 2, end - i - 2), 0);
                    i = end + 2;
                    continue;
                }
            }
        }
        if (!inBacktick && c == QLatin1Char('$')) {
            const int end = input.indexOf(QLatin1Char('$'), i + 1);
            if (end > i + 1) {
                const QString span = input.mid(i + 1, end - i - 1);
                if (!span.contains(QLatin1Char('\n')) && looksLikeMath(span)) {
                    out += convertFragment(span, 0);
                    i = end + 1;
                    continue;
                }
            }
        }
        out += c;
        ++i;
    }
    return out;
}

} // namespace MathText
} // namespace NexusAI
