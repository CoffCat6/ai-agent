#include "viewmodels/CodeHighlighter.h"

#include "viewmodels/ThemeProvider.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QTextBlock>
#include <QTextLayout>

namespace NexusAI {

CodeHighlighter::CodeHighlighter(QObject* parent)
    : QSyntaxHighlighter(parent) {
    init();
}

CodeHighlighter::CodeHighlighter(QTextDocument* document)
    : QSyntaxHighlighter(document) {
    init();
}

void CodeHighlighter::init() {
    rebuildFormats();
    connect(ThemeProvider::instance(), &ThemeProvider::themeChanged, this, [this] {
        rebuildFormats();
        rehighlight();
    });
}

void CodeHighlighter::setDocument(QQuickTextDocument* document) {
    m_document = document;
    // QSyntaxHighlighter::setDocument() rehighlights the whole document, so
    // the current language is applied whatever order QML assigns the three
    // properties in. (A manual rehighlight here was tried and is redundant:
    // removing it left every test passing.)
    QSyntaxHighlighter::setDocument(document ? document->textDocument() : nullptr);
}

void CodeHighlighter::setText(const QString& text) {
    if (m_text == text) return;
    m_text = text;
    refreshLanguage();
}

void CodeHighlighter::setSourceName(const QString& name) {
    const QString trimmed = name.trimmed();
    if (m_sourceName == trimmed) return;
    m_sourceName = trimmed;
    // The name outranks the content: switching files must not keep the
    // previous file's language while the new text is still on screen.
    refreshLanguage();
}

void CodeHighlighter::refreshLanguage() {
    const Language previous = m_language;

    // docs/9 §5 Batch 23: oversized blocks render as plain text. The budget
    // applies to both paths - a 512KB file stays plain even when its
    // extension is recognised.
    const bool tooBig = m_text.size() > MaxChars
        || m_text.count(QLatin1Char('\n')) + 1 > MaxLines;

    if (!m_sourceName.isEmpty()) {
        // Named file: the extension decides, and an unknown extension means
        // "leave it plain" rather than "guess from content". Guessing is what
        // turned CMakeLists.txt into Shell and .clang-format into Markdown.
        m_language = tooBig ? Unknown : languageForFileName(m_sourceName);
    } else {
        m_language = tooBig ? Unknown : detectLanguage(m_text);
    }

    // Content-only edits already rehighlight incrementally through the
    // document change signals; a full pass is only needed when the active
    // language (or the plain-text fallback) flips.
    if (m_language != previous) {
        rehighlight();
        emit languageNameChanged();
    }
}

bool CodeHighlighter::hasHighlightedText() const {
    // document() returns what QSyntaxHighlighter was given, i.e. a
    // QTextDocument* - reach it through the base class, not via the QML
    // wrapper (m_document is the QQuickTextDocument*).
    const QTextDocument* doc = QSyntaxHighlighter::document();
    if (!doc) return false;
    for (QTextBlock block = doc->firstBlock(); block.isValid(); block = block.next()) {
        const QTextLayout* layout = block.layout();
        if (!layout) continue;
        const QVector<QTextLayout::FormatRange> ranges = layout->formats();
        for (const auto& range : ranges) {
            if (range.length > 0
                    && range.format.foreground().style() != Qt::NoBrush)
                return true;
        }
    }
    return false;
}

CodeHighlighter::Language CodeHighlighter::languageForFileName(const QString& name) {
    // Only the basename matters, and only the part after the last dot that is
    // not the first character - so ".clang-format" has no usable extension.
    const QString base = name.section(QLatin1Char('/'), -1).section(QLatin1Char('\\'), -1);
    const int dot = base.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0 || dot == base.size() - 1) return Unknown;
    const QString ext = base.mid(dot + 1).toLower();

    // C family.
    if (ext == QLatin1String("c") || ext == QLatin1String("cc")
            || ext == QLatin1String("cpp") || ext == QLatin1String("cxx")
            || ext == QLatin1String("h") || ext == QLatin1String("hh")
            || ext == QLatin1String("hpp") || ext == QLatin1String("hxx")
            || ext == QLatin1String("inl") || ext == QLatin1String("ipp")
            || ext == QLatin1String("m") || ext == QLatin1String("mm")
            || ext == QLatin1String("cs")) {
        return Cpp;
    }
    // JS/TS family. QML rides here: it is a JS superset, so keywords, strings
    // and comments colour correctly while QML-only words (property / signal /
    // Item) stay plain. An approximation on purpose - a dedicated QML rule set
    // is not worth the upkeep yet.
    if (ext == QLatin1String("js") || ext == QLatin1String("mjs")
            || ext == QLatin1String("cjs") || ext == QLatin1String("jsx")
            || ext == QLatin1String("tsx")
            || ext == QLatin1String("vue") || ext == QLatin1String("svelte")
            || ext == QLatin1String("qml")) {
        return JsTs;
    }
    if (ext == QLatin1String("py") || ext == QLatin1String("pyi")) return Python;
    if (ext == QLatin1String("json") || ext == QLatin1String("jsonc")) return Json;
    // html/xml plus the Qt descriptor formats, which are all XML.
    if (ext == QLatin1String("html") || ext == QLatin1String("htm")
            || ext == QLatin1String("xhtml") || ext == QLatin1String("xml")
            || ext == QLatin1String("svg") || ext == QLatin1String("ui")
            || ext == QLatin1String("qrc") || ext == QLatin1String("qmltypes")) {
        return XmlHtml;
    }
    if (ext == QLatin1String("css") || ext == QLatin1String("scss")
            || ext == QLatin1String("less")) {
        return Css;
    }
    if (ext == QLatin1String("sh") || ext == QLatin1String("bash")
            || ext == QLatin1String("zsh") || ext == QLatin1String("fish")) {
        return Bash;
    }
    if (ext == QLatin1String("md") || ext == QLatin1String("markdown")) return Markdown;

    // Everything else - yaml, toml, ini, txt, cmake, log, lock - has no rule
    // set and stays plain. ".ts" lands here too, on purpose: it is XML in a Qt
    // project and TypeScript everywhere else, so either reading would be wrong
    // for half of this application's own projects. Plain beats confidently
    // wrong. (".tsx" has no such ambiguity and stays above.)
    return Unknown;
}

QString CodeHighlighter::languageDisplayName(Language language) {
    switch (language) {
    case Json:     return QStringLiteral("JSON");
    case Python:   return QStringLiteral("Python");
    case Cpp:      return QStringLiteral("C++");
    case XmlHtml:  return QStringLiteral("HTML");
    case Css:      return QStringLiteral("CSS");
    case Bash:     return QStringLiteral("Shell");
    case JsTs:     return QStringLiteral("JS/TS");
    case Markdown: return QStringLiteral("Markdown");
    default:       return QString();
    }
}

// Heuristics over the whole text; order matters — the most distinctive
// probes win. Inconclusive detection returns Unknown (no coloring).
CodeHighlighter::Language CodeHighlighter::detectLanguage(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return Unknown;

    if (trimmed.startsWith(QLatin1Char('{')) || trimmed.startsWith(QLatin1Char('['))) {
        // Strict parse is the strongest JSON signal; a partial/streamed
        // object still trips on the "key": pattern.
        QJsonParseError error{};
        if (!QJsonDocument::fromJson(text.toUtf8(), &error).isNull()) return Json;
        static const QRegularExpression keyValue(
            QStringLiteral(R"re("[^"]+"\s*:)re"));
        if (keyValue.match(text).hasMatch()) return Json;
    }
    if (trimmed.contains(QStringLiteral("<!DOCTYPE"), Qt::CaseInsensitive)
        || trimmed.contains(QLatin1String("</"))) {
        return XmlHtml;
    }
    if (trimmed.startsWith(QLatin1String("#!"))) return Bash;

    static const QRegularExpression cppPreproc(
        QStringLiteral(R"re(^\s*#\s*(?:include|define|ifndef|ifdef|endif|pragma|if|else)\b)re"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression cppMain(QStringLiteral(R"re(\bstd::\w|\bint\s+main\s*\()re"));
    static const QRegularExpression jsProbe(
        QStringLiteral(R"re(^\s*(?:const|let|var)\s+\w+\s*=|=>|\bconsole\.\w|^\s*import\s+.*\bfrom\s*['"]|^\s*function\s+\w|^\s*export\s+(?:default|const|function))re"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression pyProbe(
        QStringLiteral(R"re(^\s*(?:def\s+\w|class\s+\w+\s*[:(]|from\s+[\w.]+\s+import\s|import\s+\w+\s*$)|self\.|\bprint\s*\()re"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression bashProbe(
        QStringLiteral(R"re(\$\{?\w+\}?|^\s*(?:fi|done|esac)\s*$|^\s*(?:if|for|while|until|case)\s|^\s*(?:echo|cd|export|source|sudo|apt|pip|npm|git)\s)re"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression cssProbe(
        QStringLiteral(R"re(\{\s*[\w-]+\s*:[^;{}]+;|^\s*(?:\.|#)[\w-]+\s*\{)re"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression mdProbe(
        QStringLiteral(R"re(^#{1,6}\s+\S|^```|^\s*(?:[-*+]|\d+\.)\s+\S|\*\*[^*\n]+\*\*)re"),
        QRegularExpression::MultilineOption);

    if (cppPreproc.match(text).hasMatch() || cppMain.match(text).hasMatch()) return Cpp;
    if (jsProbe.match(text).hasMatch()) return JsTs;
    if (pyProbe.match(text).hasMatch()) return Python;
    if (bashProbe.match(text).hasMatch()) return Bash;
    if (cssProbe.match(text).hasMatch()) return Css;
    if (mdProbe.match(text).hasMatch()) return Markdown;
    return Unknown;
}

void CodeHighlighter::rebuildFormats() {
    ThemeProvider* theme = ThemeProvider::instance();
    m_formats.assign(RoleCount, QTextCharFormat{});
    m_formats[KeywordRole].setForeground(theme->brandPrimary());
    m_formats[StringRole].setForeground(theme->statusSuccess());
    QTextCharFormat& comment = m_formats[CommentRole];
    comment.setForeground(theme->textSecondary());
    comment.setFontItalic(true);
    m_formats[NumberRole].setForeground(theme->statusWarning());
    QTextCharFormat& typeName = m_formats[TypeNameRole];
    typeName.setForeground(theme->textPrimary());
    typeName.setFontWeight(QFont::DemiBold);
}

void CodeHighlighter::highlightBlock(const QString& text) {
    setCurrentBlockState(0);
    m_claimed.clear();
    if (m_language == Unknown) return;

    // The MaxLines budget counts newlines, so a 60KB minified JSON passes as
    // "one line" and would then run every rule's globalMatch across all of it.
    // Colouring one line is not worth stalling the UI for, and the file is
    // readable either way - leave it plain.
    if (text.size() > MaxBlockChars) return;

    switch (m_language) {
    case Cpp:
    case Css:
    case JsTs:
    case Json:
        highlightCStyleComment(text, QStringLiteral("/*"), QStringLiteral("*/"));
        break;
    case XmlHtml:
        highlightCStyleComment(text, QStringLiteral("<!--"), QStringLiteral("-->"));
        break;
    case Python:
        highlightTripleQuoted(text);
        break;
    case Markdown:
        highlightFence(text);
        break;
    default:
        break;
    }

    // Line rules run after the multi-line claim pass; earlier claims win,
    // so keywords inside strings/comments keep the container color.
    for (const Rule& rule : rulesFor(m_language)) {
        QRegularExpressionMatchIterator matches = rule.pattern.globalMatch(text);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            const int start = match.capturedStart();
            const int length = match.capturedLength();
            if (isClaimed(start, length)) continue;
            claim(start, length, rule.role);
        }
    }
}

void CodeHighlighter::claim(int start, int length, int role) {
    if (length <= 0 || start < 0) return;
    setFormat(start, length, m_formats[role]);
    m_claimed.append({start, length});
}

bool CodeHighlighter::isClaimed(int start, int length) const {
    const int end = start + length;
    for (const auto& span : m_claimed) {
        if (start < span.second && span.first < end) return true;
    }
    return false;
}

void CodeHighlighter::highlightCStyleComment(const QString& text, const QString& openToken,
                                             const QString& closeToken) {
    int offset = 0;
    if (previousBlockState() == 1) {
        const int close = text.indexOf(closeToken);
        if (close < 0) {
            claim(0, text.length(), CommentRole);
            setCurrentBlockState(1);
            return;
        }
        claim(0, close + closeToken.length(), CommentRole);
        offset = close + closeToken.length();
    }
    for (int open = text.indexOf(openToken, offset); open >= 0;
         open = text.indexOf(openToken, offset)) {
        const int close = text.indexOf(closeToken, open + openToken.length());
        if (close < 0) {
            claim(open, text.length() - open, CommentRole);
            setCurrentBlockState(1);
            return;
        }
        const int end = close + closeToken.length();
        claim(open, end - open, CommentRole);
        offset = end;
    }
}

void CodeHighlighter::highlightTripleQuoted(const QString& text) {
    static const QString doubleQuote = QStringLiteral("\"\"\"");
    static const QString singleQuote = QStringLiteral("'''");
    int offset = 0;
    if (previousBlockState() == 1) {
        const int dClose = text.indexOf(doubleQuote);
        const int sClose = text.indexOf(singleQuote);
        if (dClose < 0 && sClose < 0) {
            claim(0, text.length(), StringRole);
            setCurrentBlockState(1);
            return;
        }
        const int close = (sClose < 0 || (dClose >= 0 && dClose < sClose)) ? dClose : sClose;
        claim(0, close + 3, StringRole);
        offset = close + 3;
        setCurrentBlockState(0);
    }
    while (offset <= text.length()) {
        const int dOpen = text.indexOf(doubleQuote, offset);
        const int sOpen = text.indexOf(singleQuote, offset);
        if (dOpen < 0 && sOpen < 0) return;
        const int open = (sOpen < 0 || (dOpen >= 0 && dOpen < sOpen)) ? dOpen : sOpen;
        const bool openDouble = (open == dOpen) && dOpen >= 0;
        const QString& token = openDouble ? doubleQuote : singleQuote;
        const int close = text.indexOf(token, open + 3);
        if (close < 0) {
            claim(open, text.length() - open, StringRole);
            setCurrentBlockState(1);
            return;
        }
        claim(open, close + 3 - open, StringRole);
        offset = close + 3;
    }
}

void CodeHighlighter::highlightFence(const QString& text) {
    const bool fenceLine = text.startsWith(QLatin1String("```"));
    if (previousBlockState() == 1) {
        if (fenceLine) {
            claim(0, text.length(), KeywordRole);   // closing fence
            setCurrentBlockState(0);
        } else {
            claim(0, text.length(), StringRole);    // fenced content
            setCurrentBlockState(1);
        }
        return;
    }
    if (fenceLine) {
        claim(0, text.length(), KeywordRole);
        setCurrentBlockState(1);
    }
}

namespace {

const QRegularExpression rx(const char* pattern,
                            QRegularExpression::PatternOptions options
                                = QRegularExpression::NoPatternOption) {
    QRegularExpression re(QString::fromLatin1(pattern));
    re.setPatternOptions(options);
    return re;
}

} // namespace

const QVector<CodeHighlighter::Rule>& CodeHighlighter::rulesFor(Language language) {
    // Line rules apply in order; first claim of a span wins.
    static const QVector<Rule> cppRules = {
        {rx(R"re(//[^\n]*)re"), CommentRole},
        {rx(R"re("(?:[^"\\\n]|\\.)*")re"), StringRole},
        {rx(R"re('(?:[^'\\\n]|\\.)*')re"), StringRole},
        {rx(R"re(\b(?:alignas|alignof|auto|bool|break|case|catch|char|class|co_await|co_return|co_yield|concept|const|const_cast|consteval|constexpr|constinit|continue|decltype|default|delete|do|double|dynamic_cast|else|enum|explicit|export|extern|false|final|float|for|friend|goto|if|inline|int|long|mutable|namespace|new|noexcept|nullptr|operator|override|private|protected|public|register|reinterpret_cast|requires|return|short|signed|sizeof|static|static_assert|static_cast|struct|switch|template|this|thread_local|throw|true|try|typedef|typeid|typename|union|unsigned|using|virtual|void|volatile|wchar_t|while)\b)re"), KeywordRole},
        {rx(R"re(^\s*#\s*\w+)re"), KeywordRole},
        {rx(R"re(\b(?:0[xX][0-9a-fA-F]+|\d+(?:\.\d+)?)(?:[uUlLfF]+)?\b)re"), NumberRole},
        {rx(R"re(\b[A-Z]\w*)re"), TypeNameRole},
    };
    static const QVector<Rule> pythonRules = {
        {rx(R"re(#[^\n]*)re"), CommentRole},
        {rx(R"re("(?:[^"\\\n]|\\.)*")re"), StringRole},
        {rx(R"re('(?:[^'\\\n]|\\.)*')re"), StringRole},
        {rx(R"re(\b(?:and|as|assert|async|await|break|class|continue|def|del|elif|else|except|False|finally|for|from|global|if|import|in|is|lambda|None|nonlocal|not|or|pass|raise|return|True|try|while|with|yield|self|cls)\b)re"), KeywordRole},
        {rx(R"re(\b\d+(?:\.\d+)?\b)re"), NumberRole},
        {rx(R"re(\b[A-Z]\w*)re"), TypeNameRole},
    };
    static const QVector<Rule> jsonRules = {
        {rx(R"re(//[^\n]*)re"), CommentRole},                    // jsonc tolerance
        {rx(R"re("(?:[^"\\]|\\.)*"(?=\s*:))re"), KeywordRole},   // keys
        {rx(R"re("(?:[^"\\]|\\.)*")re"), StringRole},
        {rx(R"re(\b(?:true|false|null)\b)re"), KeywordRole},
        {rx(R"re(-?\b\d+(?:\.\d+)?(?:[eE][+-]?\d+)?\b)re"), NumberRole},
    };
    static const QVector<Rule> xmlRules = {
        {rx(R"re(</?[\w:.-]+|/?>)re"), KeywordRole},
        {rx(R"re("(?:[^"\\\n]|\\.)*")re"), StringRole},
    };
    static const QVector<Rule> cssRules = {
        {rx(R"re("(?:[^"\\\n]|\\.)*")re"), StringRole},
        {rx(R"re('(?:[^'\\\n]|\\.)*')re"), StringRole},
        {rx(R"re(#[0-9a-fA-F]{3,8}\b)re"), NumberRole},          // color literals
        {rx(R"re(\b\d+(?:\.\d+)?(?:px|em|rem|vh|vw|ms|s|pt|%)?\b)re"), NumberRole},
        {rx(R"re([\w-]+(?=\s*:))re"), KeywordRole},              // property names
    };
    static const QVector<Rule> bashRules = {
        {rx(R"re(#[^\n]*)re"), CommentRole},
        {rx(R"re("(?:[^"\\\n]|\\.)*")re"), StringRole},
        {rx(R"re('[^'\n]*')re"), StringRole},
        {rx(R"re(\$\{?\w+\}?)re"), TypeNameRole},                // variables
        {rx(R"re(\b(?:if|then|else|elif|fi|for|while|until|do|done|case|esac|function|select|in|return|exit|local|export|readonly|declare|echo|cd|source|set|shift|trap|getopts|shopt|alias|unalias)\b)re"), KeywordRole},
        {rx(R"re(\b\d+\b)re"), NumberRole},
    };
    static const QVector<Rule> jsRules = {
        {rx(R"re(//[^\n]*)re"), CommentRole},
        {rx(R"re("(?:[^"\\\n]|\\.)*")re"), StringRole},
        {rx(R"re('(?:[^'\\\n]|\\.)*')re"), StringRole},
        {rx(R"re(`(?:[^`\\]|\\.)*`)re"), StringRole},             // template literal
        {rx(R"re(\b(?:abstract|any|as|async|await|boolean|break|case|catch|class|const|constructor|continue|debugger|declare|default|delete|do|else|enum|export|extends|false|finally|for|from|function|get|if|implements|import|in|infer|instanceof|interface|is|keyof|let|namespace|never|new|null|number|object|of|private|protected|public|readonly|return|set|static|string|super|switch|symbol|this|throw|true|try|type|typeof|undefined|unknown|var|void|while|with|yield)\b)re"), KeywordRole},
        {rx(R"re(\b\d+(?:\.\d+)?\b)re"), NumberRole},
        {rx(R"re(\b[A-Z]\w*)re"), TypeNameRole},
    };
    static const QVector<Rule> markdownRules = {
        {rx(R"re(^#{1,6}\s[^\n]*)re", QRegularExpression::MultilineOption), KeywordRole},   // headings
        {rx(R"re(^>[^\n]*)re", QRegularExpression::MultilineOption), CommentRole},          // quotes
        {rx(R"re(^\s*(?:[-*+]|\d+\.)\s)re", QRegularExpression::MultilineOption), KeywordRole}, // list markers
        {rx(R"re(\*\*[^*\n]+\*\*)re"), TypeNameRole},                                       // bold
        {rx(R"re(`[^`\n]+`)re"), StringRole},                                               // inline code
        {rx(R"re(\[[^\]\n]*\]\([^)\n]*\))re"), KeywordRole},                                // links
    };
    static const QVector<Rule> noRules;

    switch (language) {
    case Cpp: return cppRules;
    case Python: return pythonRules;
    case Json: return jsonRules;
    case XmlHtml: return xmlRules;
    case Css: return cssRules;
    case Bash: return bashRules;
    case JsTs: return jsRules;
    case Markdown: return markdownRules;
    default: return noRules;
    }
}

} // namespace NexusAI
