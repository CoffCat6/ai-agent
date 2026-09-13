// Batch 23 (docs/9 F11): CodeHighlighter — language detection coverage,
// token formats (QTextLayout::formats), plain-text fallbacks (unknown
// language, >64KB, >2000 lines) and theme-switch rehighlight.
#include "viewmodels/CodeHighlighter.h"
#include "viewmodels/ThemeManager.h"

#include <QGuiApplication>
#include <QTextBlock>
#include <QTextLayout>
#include <QtTest>

using namespace NexusAI;

namespace {

// Every format range the highlighter left anywhere in the document.
QVector<QTextLayout::FormatRange> allFormats(const QTextDocument& doc) {
    QVector<QTextLayout::FormatRange> out;
    for (QTextBlock block = doc.firstBlock(); block.isValid(); block = block.next())
        out += block.layout()->formats();
    return out;
}

// True when block |blockNumber| carries a colored format range covering
// |needle|. FormatRange offsets are block-relative, so checks must be done
// per block (a whole-document span would mix offsets from other blocks).
bool blockCovers(const QTextDocument& doc, int blockNumber, const QString& needle) {
    const QTextBlock block = doc.findBlockByNumber(blockNumber);
    if (!block.isValid()) return false;
    const int pos = block.text().indexOf(needle);
    if (pos < 0) return false;
    for (const auto& range : block.layout()->formats()) {
        if (range.start <= pos && pos + needle.size() <= range.start + range.length
            && range.format.foreground().style() != Qt::NoBrush) {
            return true;
        }
    }
    return false;
}

// ThemeManager persists ui.themeMode through QSettings; restore on exit.
class ThemeGuard {
public:
    explicit ThemeGuard(bool currentDark) : m_restore(currentDark) {}
    ~ThemeGuard() { ThemeManager::instance()->setIsDark(m_restore); }
    ThemeGuard(const ThemeGuard&) = delete;
    ThemeGuard& operator=(const ThemeGuard&) = delete;

private:
    bool m_restore;
};

} // namespace

class TestCodeHighlighter : public QObject {
    Q_OBJECT

private slots:
    void jsonCoverage() {
        const QString src = QStringLiteral(R"({
    // session config
    "name": "agent",
    "retries": 3,
    "verbose": true
})");
        QTextDocument doc;
        doc.setPlainText(src);
        CodeHighlighter highlighter(&doc);   // text set after attach
        highlighter.setText(src);

        const auto formats = allFormats(doc);
        QVERIFY(!formats.isEmpty());
        QVERIFY(blockCovers(doc, 2, "\"name\""));          // key
        QVERIFY(blockCovers(doc, 2, "agent"));             // string value
        QVERIFY(blockCovers(doc, 4, "true"));              // keyword
        QVERIFY(blockCovers(doc, 3, "3"));                 // number
        QVERIFY(blockCovers(doc, 1, "// session config")); // jsonc comment
    }

    void pythonCoverage() {
        const QString src = QStringLiteral("def greet(name):\n"
                                           "    \"\"\"Docstring\n"
                                           "    spanning lines.\"\"\"\n"
                                           "    count = 42\n"
                                           "    print(f\"hello {name}\")\n"
                                           "    return True\n");
        QTextDocument doc;
        doc.setPlainText(src);
        CodeHighlighter highlighter(&doc);   // document attached before text
        highlighter.setText(src);

        const auto formats = allFormats(doc);
        QVERIFY(!formats.isEmpty());
        QVERIFY(blockCovers(doc, 0, "def"));     // keyword
        QVERIFY(blockCovers(doc, 3, "42"));      // number
        QVERIFY(blockCovers(doc, 5, "True"));    // keyword
        QVERIFY(blockCovers(doc, 4, "hello"));   // string
        // Triple-quoted docstring colors the continuation block too.
        QVERIFY(blockCovers(doc, 2, "spanning"));
    }

    void cppCoverage() {
        const QString src = QStringLiteral("#include <QString>\n"
                                           "int main() {\n"
                                           "    // check the count\n"
                                           "    const int count = 0x1F;\n"
                                           "    return 0;\n"
                                           "}\n");
        QTextDocument doc;
        doc.setPlainText(src);
        CodeHighlighter highlighter(&doc);
        highlighter.setText(src);

        const auto formats = allFormats(doc);
        QVERIFY(!formats.isEmpty());
        {
            const QTextBlock dbg = doc.findBlockByNumber(1);
            qWarning() << "DBG block1 text:" << dbg.text();
            for (const auto& r : dbg.layout()->formats())
                qWarning() << "DBG fmt" << r.start << r.length << r.format.foreground().color();
        }
        QVERIFY(blockCovers(doc, 1, "int"));                // keyword
        QVERIFY(blockCovers(doc, 3, "const"));              // keyword
        QVERIFY(blockCovers(doc, 3, "0x1F"));               // number
        QVERIFY(blockCovers(doc, 0, "QString"));            // type name
        QVERIFY(blockCovers(doc, 2, "// check the count")); // comment
        QVERIFY(blockCovers(doc, 0, "#include"));           // preprocessor
    }

    void multilineCommentSpansBlocks() {
        const QString src = QStringLiteral("#include <QDebug>\n"
                                           "int a; /* comment start\n"
                                           "still comment\n"
                                           "end */ int b;");
        QTextDocument doc;
        doc.setPlainText(src);
        CodeHighlighter highlighter(&doc);
        highlighter.setText(src);

        QVERIFY(blockCovers(doc, 1, "int"));
        QVERIFY(blockCovers(doc, 2, "still"));
        QVERIFY(blockCovers(doc, 3, "end */"));
        QVERIFY(blockCovers(doc, 3, "int"));
    }

    void markdownFenceSpansBlocks() {
        const QString src = QStringLiteral("```text\n"
                                           "plain notes with #hash\n"
                                           "> quoted line\n"
                                           "```\n");
        QTextDocument doc;
        doc.setPlainText(src);
        CodeHighlighter highlighter(&doc);
        highlighter.setText(src);

        const auto formats = allFormats(doc);
        QVERIFY(blockCovers(doc, 0, "```text"));     // fence line
        QVERIFY(blockCovers(doc, 1, "plain notes")); // fenced content
        QVERIFY(blockCovers(doc, 2, "quoted line")); // fenced content, 2nd block
        QVERIFY(blockCovers(doc, 3, "```"));         // closing fence
    }

    void unknownLanguageStaysPlain() {
        const QString src = QStringLiteral("This is a plain English sentence.\n"
                                           "Nothing to see here, move along.\n");
        QTextDocument doc;
        doc.setPlainText(src);
        CodeHighlighter highlighter(&doc);
        highlighter.setText(src);

        QVERIFY(allFormats(doc).isEmpty());
    }

    void oversizedFallsBackToPlain() {
        QString src;
        while (src.size() <= 64 * 1024)
            src += QStringLiteral("plain text without any code markers ");
        QVERIFY(src.size() > 64 * 1024);

        QTextDocument doc;
        doc.setPlainText(src);
        CodeHighlighter highlighter(&doc);
        highlighter.setText(src);

        QVERIFY(allFormats(doc).isEmpty());
    }

    void tooManyLinesFallsBackToPlain() {
        QString src;
        for (int i = 0; i < 2001; ++i)
            src += QStringLiteral("line\n");
        QVERIFY(src.count(QLatin1Char('\n')) + 1 > 2000);

        QTextDocument doc;
        doc.setPlainText(src);
        CodeHighlighter highlighter(&doc);
        highlighter.setText(src);

        QVERIFY(allFormats(doc).isEmpty());
    }

    void themeSwitchRethighlights() {
        const bool originalDark = ThemeManager::instance()->isDark();
        ThemeGuard guard(originalDark);

        const QString src = QStringLiteral("int main() { return 0; }");
        QTextDocument doc;
        doc.setPlainText(src);
        CodeHighlighter highlighter(&doc);
        highlighter.setText(src);
        const auto before = allFormats(doc);
        QVERIFY(!before.isEmpty());

        ThemeManager::instance()->setIsDark(!originalDark);   // themeChanged → rehighlight

        const auto after = allFormats(doc);
        QVERIFY(!after.isEmpty());
        QVERIFY(blockCovers(doc, 0, "int"));
        QVERIFY(blockCovers(doc, 0, "return"));
    }
};

int main(int argc, char* argv[]) {
    QCoreApplication::setOrganizationName("NexusAI");
    QCoreApplication::setApplicationName("NexusAI");
    QGuiApplication app(argc, argv);
    TestCodeHighlighter tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_highlighter.moc"
