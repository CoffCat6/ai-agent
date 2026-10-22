#pragma once

#include <QPair>
#include <QQmlEngine>
#include <QQuickTextDocument>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QVector>

namespace NexusAI {

// Batch 23 (docs/9 §5 F11): regex tokenizer behind the chat/trace code
// blocks. Language is auto-detected from content heuristics (json/python/
// cpp/xml-html/css/bash/js-ts/markdown); inconclusive detection stays plain
// so prose never gets spurious color. Five token classes map to Theme
// tokens with distinct dark/light values; a themeChanged rehighlight keeps
// blocks in sync. Oversized blocks fall back to plain text (docs/9 §5
// Batch 23 performance bound). Self-built on purpose: no third-party
// highlighter (offline / license / size).
class CodeHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
    QML_ELEMENT
    QML_NAMED_ELEMENT(CodeHighlighter)
    // Write-only drive pair from QML:
    //   CodeHighlighter { document: textEdit.textDocument; text: codeText }
    Q_PROPERTY(QQuickTextDocument* document READ document WRITE setDocument)
    Q_PROPERTY(QString text READ text WRITE setText)
    // docs/19 §5.2: 派生的语言标签（"JSON"/"Python"/...），未知语言为空串。
    Q_PROPERTY(QString languageName READ languageName NOTIFY languageNameChanged)
    // Optional file name (or path) whose extension pins the language. Empty =
    // fall back to content heuristics, which is what the chat code blocks need
    // (they only ever have text).
    //
    // The sidebar preview has the file name, and the heuristics are
    // demonstrably wrong on real files: CMakeLists.txt detects as Shell,
    // .clang-format as Markdown. With a name supplied, an unrecognised
    // extension leaves the block PLAIN instead of guessing - a wrong colour is
    // worse than none, and it would make the preview actively misleading.
    Q_PROPERTY(QString sourceName READ sourceName WRITE setSourceName)
    // Callers use this to explain why a long file lost its colours.
    Q_PROPERTY(int maxHighlightChars READ maxHighlightChars CONSTANT)

public:
    explicit CodeHighlighter(QObject* parent = nullptr);
    // C++ side attaches straight to a QTextDocument; QML uses the
    // document/text properties below instead.
    explicit CodeHighlighter(QTextDocument* document);

    // QML passes textEdit.textDocument (null on teardown is fine).
    QQuickTextDocument* document() const { return m_document; }
    void setDocument(QQuickTextDocument* document);
    // Full text of the block: feeds detection and the size/line budget.
    // Content edits inside the document rehighlight incrementally on their
    // own; a full rehighlight only happens when language/threshold flips.
    QString text() const { return m_text; }
    void setText(const QString& text);

    QString sourceName() const { return m_sourceName; }
    void setSourceName(const QString& name);
    static int maxHighlightChars() { return MaxChars; }

    // True when the attached document actually carries at least one coloured
    // range. QML cannot walk QTextDocument blocks (they are not exposed), so a
    // test cannot otherwise tell "language recognised" from "colours applied";
    // this is that observation point. Also handy for diagnostics.
    Q_INVOKABLE bool hasHighlightedText() const;

    // 供代码块标题栏显示的语言名；Unknown → 空串（调用方据此隐藏标题栏）。
    QString languageName() const { return languageDisplayName(m_language); }

signals:
    // 仅在检测出的语言真正翻转时发出，避免流式期间标题栏反复重绑。
    void languageNameChanged();

protected:
    void highlightBlock(const QString& text) override;

private:
    enum Language {
        Unknown,
        Json,
        Python,
        Cpp,
        XmlHtml,
        Css,
        Bash,
        JsTs,
        Markdown,
    };
    enum FormatRole { KeywordRole, StringRole, CommentRole, NumberRole, TypeNameRole, RoleCount };

    struct Rule {
        QRegularExpression pattern;
        int role;
    };

    static Language detectLanguage(const QString& text);
    // Extension-driven mapping for the file preview. Unknown for anything
    // without a rule set (yaml/toml/ini/cmake/...): those stay plain.
    static Language languageForFileName(const QString& name);
    static const QVector<Rule>& rulesFor(Language language);
    // 标题栏用的稳定语言名；Unknown → 空串。
    static QString languageDisplayName(Language language);

    void rebuildFormats();
    void init();
    // Recompute m_language from sourceName (if set) or the content heuristics,
    // rehighlighting when it actually flips. Shared by setText/setSourceName
    // so the two properties cannot disagree about precedence.
    void refreshLanguage();
    void claim(int start, int length, int role);
    bool isClaimed(int start, int length) const;
    // /* */ / <!-- --> state machine shared by the C-family comment tokens.
    void highlightCStyleComment(const QString& text, const QString& openToken,
                                const QString& closeToken);
    // Python triple-quoted strings (""" or '''), block-state driven.
    void highlightTripleQuoted(const QString& text);
    // Markdown ``` fences: whole fenced blocks render in string color.
    void highlightFence(const QString& text);

    QString m_text;
    QString m_sourceName;
    QQuickTextDocument* m_document = nullptr;
    Language m_language = Unknown;
    QVector<QTextCharFormat> m_formats;               // indexed by FormatRole
    QVector<QPair<int, int>> m_claimed;               // spans colored this block
    static constexpr int MaxChars = 64 * 1024;
    static constexpr int MaxLines = 2000;
    // One line may not be longer than this. MaxLines counts newlines, so a
    // 60KB minified JSON passes as a single line and would then run every
    // rule's globalMatch across the whole thing.
    static constexpr int MaxBlockChars = 8 * 1024;
};

} // namespace NexusAI
