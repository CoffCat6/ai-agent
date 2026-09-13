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
    static const QVector<Rule>& rulesFor(Language language);

    void rebuildFormats();
    void init();
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
    QQuickTextDocument* m_document = nullptr;
    Language m_language = Unknown;
    QVector<QTextCharFormat> m_formats;               // indexed by FormatRole
    QVector<QPair<int, int>> m_claimed;               // spans colored this block
    static constexpr int MaxChars = 64 * 1024;
    static constexpr int MaxLines = 2000;
};

} // namespace NexusAI
