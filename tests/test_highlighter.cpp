// Batch 23 (docs/9 F11): CodeHighlighter — language detection coverage,
// token formats (QTextLayout::formats), plain-text fallbacks (unknown
// language, >64KB, >2000 lines) and theme-switch rehighlight.
#include "viewmodels/CitationParser.h"
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

    // docs/20 §5.1 (CITE-801): 引用记号 "[文档名]" 的纯函数切分。
    // 这是唯一实现 —— AssistantMessage.qml 通过 CitationParser QML 单例
    // 调用同一份代码，QML 侧不再有 JS 镜像。
    void citationMarkerParsing() {
        const QString src = QStringLiteral(
            "结论甲 [文档A.md]。结论乙 [报告 B]，收尾 [终稿.txt]。");
        const auto segs = Citation::parse(src);

        QVERIFY(Citation::hasCitation(segs));
        // 文本/引用交替，引用 3 个。
        QCOMPARE(segs.size(), 7);
        QCOMPARE(segs[0].text, QStringLiteral("结论甲 "));
        QVERIFY(!segs[0].isCitation);
        QVERIFY(segs[1].isCitation);
        QCOMPARE(segs[1].docName, QStringLiteral("文档A.md"));
        QCOMPARE(segs[1].text, segs[1].docName);
        QVERIFY(segs[3].isCitation);
        QCOMPARE(segs[3].docName, QStringLiteral("报告 B"));
        QVERIFY(segs[5].isCitation);
        QCOMPARE(segs[5].docName, QStringLiteral("终稿.txt"));
        QVERIFY(!segs[6].isCitation);

        // 引用记号只切掉方括号定界符，散文文字一字不丢。
        QString joined;
        for (const auto& s : segs) joined += s.text;
        QCOMPARE(joined, QStringLiteral("结论甲 文档A.md。结论乙 报告 B，收尾 终稿.txt。"));

        // 无记号 → 单个普通片段，hasCitation 为假。
        const auto plain = Citation::parse(QStringLiteral("没有任何记号的句子。"));
        QCOMPARE(plain.size(), 1);
        QVERIFY(!plain[0].isCitation);
        QVERIFY(!Citation::hasCitation(plain));
    }

    // docs/25: 完整 provenance 记号 [doc:文档名#chunkId] 归一化——docName
    // 剥前缀与 #N、chunkIndex 记 0 基分块号；简写记号 chunkIndex = -1。
    void citationMarkerWithChunkIndex() {
        const auto segs = Citation::parse(
            QStringLiteral("出处 [doc:报告 B#3] 与另一处 [报告 B]。"));

        QVERIFY(segs[1].isCitation);
        QCOMPARE(segs[1].docName, QStringLiteral("报告 B"));   // 前缀+#N 剥离
        QCOMPARE(segs[1].chunkIndex, 3);
        // 简写记号：归一化文档名相同（两种书写指向同一文档），无分块。
        QVERIFY(segs[3].isCitation);
        QCOMPARE(segs[3].docName, QStringLiteral("报告 B"));
        QCOMPARE(segs[3].chunkIndex, -1);

        // 无损：拼接回原文一字不丢。
        QString joined;
        for (const auto& s : segs) joined += s.text;
        QCOMPARE(joined, QStringLiteral("出处 doc:报告 B#3 与另一处 报告 B。"));

        // 文档名含 '#' 但末段不是纯数字：整体保留为文档名（KnowledgeSearchTool
        // 同一约定——chunkId 取最后一个 '#' 后的数字）。注意整行只有记号时
        // 没有前导文本段，引用就是 segments[0]。
        const auto hashName = Citation::parse(QStringLiteral("[doc:C++#笔记]"));
        QCOMPARE(hashName.size(), 1);
        QVERIFY(hashName[0].isCitation);
        QCOMPARE(hashName[0].docName, QStringLiteral("doc:C++#笔记"));
        QCOMPARE(hashName[0].chunkIndex, -1);

        // 非 ASCII 数字（全角 '３'）不是合法 chunkId：QChar::isDigit 会接受
        // 它，但 toInt 解析失败——若只看 isDigit 会静默得到 0，把引用误定位
        // 到第 1 个分块。这里必须整体保留为文档名、chunkIndex 为 -1。
        const auto fullWidth = Citation::parse(QStringLiteral("[doc:全角#３]"));
        QCOMPARE(fullWidth.size(), 1);
        QVERIFY(fullWidth[0].isCitation);
        QCOMPARE(fullWidth[0].docName, QStringLiteral("doc:全角#３"));
        QCOMPARE(fullWidth[0].chunkIndex, -1);

        // 溢出（超出 int 范围）同样拒绝，不静默取 0。
        const auto overflow = Citation::parse(
            QStringLiteral("[doc:大数#99999999999999999999]"));
        QCOMPARE(overflow.size(), 1);
        QVERIFY(overflow[0].isCitation);
        QCOMPARE(overflow[0].docName,
                 QStringLiteral("doc:大数#99999999999999999999"));
        QCOMPARE(overflow[0].chunkIndex, -1);
    }

    // 拒绝规则（CitationParser.h 规则 1–5）。
    //
    // ⚠ 这里必须逐段断言「没有任何片段是引用」，不能只断言「不是单个引用
    // 片段」：曾经的 only() 辅助函数用的是后者，于是 [ref][def] 被拆成
    // [text:[ref]] + [CITE:def]（两段）时也判成 not-citation 而通过了 ——
    // 而规则 4 的意图是引用定义整组都不识别。test_citation_qml 的逐段断言
    // 抓到了这个漏网行为，已修。
    void citationMarkerEdgeCases() {
        auto noneCited = [](const QString& s) {
            const auto segs = Citation::parse(s);
            for (const auto& g : segs) {
                if (g.isCitation)
                    return QStringLiteral("CITED:%1").arg(g.docName);
            }
            // 无损：原文必须一字不丢地回来。
            QString joined;
            for (const auto& g : segs) joined += g.text;
            return joined == s ? QStringLiteral("not-citation")
                               : QStringLiteral("LOSSY:%1").arg(joined);
        };

        // markdown 链接（']' 后紧跟 '('）与引用定义（']' 后紧跟 '['）不识别。
        QCOMPARE(noneCited(QStringLiteral("[标题](http://x)")), QStringLiteral("not-citation"));
        QCOMPARE(noneCited(QStringLiteral("[ref][def]")), QStringLiteral("not-citation"));
        // 图片语法（'[' 前是 '!'）不识别。
        QCOMPARE(noneCited(QStringLiteral("![alt](y)")), QStringLiteral("not-citation"));
        // 内容未 trim（前后有空格）不识别。
        QCOMPARE(noneCited(QStringLiteral("[ 文档A ]")), QStringLiteral("not-citation"));
        // 未闭合的 '['。
        QCOMPARE(noneCited(QStringLiteral("前 [文档A 后文")), QStringLiteral("not-citation"));
        // 嵌套方括号整组拒绝，不产生内部引用。
        QCOMPARE(noneCited(QStringLiteral("[外 [内] 尾]")), QStringLiteral("not-citation"));
        // 80 字符放行，81 字符拒绝（拒后按普通文本保留原文）。
        const QString name80(80, QLatin1Char('a'));
        const QString name81(81, QLatin1Char('a'));
        QVERIFY(Citation::parse(QStringLiteral("[") + name80 + QLatin1Char(']'))[0].isCitation);
        const auto longSegs =
            Citation::parse(QStringLiteral("[") + name81 + QLatin1Char(']'));
        QCOMPARE(longSegs.size(), 1);
        QVERIFY(!longSegs[0].isCitation);
        QCOMPARE(longSegs[0].text, QStringLiteral("[") + name81 + QLatin1Char(']'));
        // 空内容不识别。
        QCOMPARE(noneCited(QStringLiteral("[]")), QStringLiteral("not-citation"));

        // 反向守卫：中间有空格的两组方括号是**独立**记号，都要识别。
        // 这条防止「跳过整组 markdown 构造」的修复误伤正常引用。
        const auto spaced = Citation::parse(QStringLiteral("[a] [b]"));
        QCOMPARE(spaced.size(), 3);
        QVERIFY(spaced[0].isCitation);
        QCOMPARE(spaced[0].docName, QStringLiteral("a"));
        QVERIFY(spaced[2].isCitation);
        QCOMPARE(spaced[2].docName, QStringLiteral("b"));
        QCOMPARE(spaced[1].text, QStringLiteral(" "));
    }

    // QML 单例封装（CitationParser::parse）必须与纯函数语义一致：
    // AssistantMessage.qml 走的是前者，两边一旦漂移 UI 就会静默失效。
    void citationQmlSingletonMatchesPureFunction() {
        const QString src = QStringLiteral(
            "结论甲 [文档A.md]。链接 [标题](http://x)，脚注 [1]，[TODO]。");
        const QVector<Citation::Segment> segs = Citation::parse(src);

        const QVariantList qml = CitationParser::instance()->parse(src);
        QCOMPARE(qml.size(), segs.size());
        for (int i = 0; i < segs.size(); ++i) {
            const QVariantMap m = qml[i].toMap();
            QCOMPARE(m.value(QStringLiteral("text")).toString(), segs[i].text);
            QCOMPARE(m.value(QStringLiteral("citation")).toBool(), segs[i].isCitation);
            QCOMPARE(m.value(QStringLiteral("name")).toString(), segs[i].docName);
        }

        // 键名是 QML 侧使用的字面量（modelData.citation / .name / .text）。
        QVERIFY(qml[0].toMap().contains(QStringLiteral("citation")));
        QVERIFY(qml[0].toMap().contains(QStringLiteral("name")));

        // hasCitation 走 QVariantList 也要给出同样的答案。
        QCOMPARE(CitationParser::instance()->hasCitation(qml),
                 Citation::hasCitation(segs));
        QVERIFY(CitationParser::instance()->hasCitation(qml));

        const QVariantList none =
            CitationParser::instance()->parse(QStringLiteral("没有任何记号。"));
        QCOMPARE(none.size(), 1);
        QVERIFY(!CitationParser::instance()->hasCitation(none));
    }

    // ── file-name driven language (sidebar preview) ──────
    //
    // The content heuristics are fine for chat code blocks (text only), but
    // they mislabel real files - measured on this repo: CMakeLists.txt came
    // out as Shell and .clang-format as Markdown. When the caller knows the
    // file name, the extension decides, and an unrecognised extension stays
    // plain rather than being guessed at.

    void sourceNameOverridesHeuristics() {
        // Content that would trip the heuristics one way ...
        const QString src = QStringLiteral("if (CMAKE_BUILD_TYPE)\n"
                                           "  message(STATUS \"x\")\n"
                                           "endif()\n");
        {
            // ... without a name, it guesses (wrongly).
            QTextDocument doc;
            doc.setPlainText(src);
            CodeHighlighter highlighter(&doc);
            highlighter.setText(src);
            QVERIFY2(!highlighter.languageName().isEmpty(),
                     "前提：内容启发式会给出某个判定");
        }

        // With the name, an extension that has no rule set means plain.
        QTextDocument doc;
        doc.setPlainText(src);
        CodeHighlighter highlighter(&doc);
        highlighter.setSourceName(QStringLiteral("CMakeLists.txt"));
        highlighter.setText(src);
        QCOMPARE(highlighter.languageName(), QString());
        QVERIFY2(allFormats(doc).isEmpty(), "未知扩展名不应上色");
    }

    void sourceNameMapsKnownExtensions() {
        struct Case { const char* name; const char* expected; };
        const Case cases[] = {
            {"src/ui/main.qml", "JS/TS"},          // QML approximated as JS
            {"src/core/agent/AIAgent.cpp", "C++"},
            {"src/viewmodels/CodeHighlighter.h", "C++"},
            {"script.py", "Python"},
            {"data.json", "JSON"},
            {"index.html", "HTML"},
            {"form.ui", "HTML"},                   // Qt .ui is XML
            {"style.css", "CSS"},
            {"run.sh", "Shell"},
            {"readme.md", "Markdown"},
            // No rule set -> plain, even though these look like something.
            {"config.yaml", ""},
            {"Cargo.toml", ""},
            {"notes.txt", ""},
            {".clang-format", ""},                 // dotfile: no extension
            {"translation.ts", ""},                // XML in Qt, TS elsewhere: ambiguous
        };
        for (const Case& c : cases) {
            QTextDocument doc;
            doc.setPlainText(QStringLiteral("placeholder\n"));
            CodeHighlighter highlighter(&doc);
            highlighter.setSourceName(QString::fromLatin1(c.name));
            highlighter.setText(QStringLiteral("placeholder\n"));
            QCOMPARE(highlighter.languageName(), QString::fromLatin1(c.expected));
        }
    }

    // The name wins over content even when the content looks like something
    // else - a .py file full of JSON-looking text is still Python.
    void sourceNameWinsOverConflictingContent() {
        const QString jsonish = QStringLiteral("{\"key\": \"value\", \"n\": 1}\n");
        QTextDocument doc;
        doc.setPlainText(jsonish);
        CodeHighlighter highlighter(&doc);
        highlighter.setSourceName(QStringLiteral("payload.py"));
        highlighter.setText(jsonish);
        QCOMPARE(highlighter.languageName(), QStringLiteral("Python"));

        // Switching files re-evaluates immediately, without waiting for text.
        highlighter.setSourceName(QStringLiteral("payload.json"));
        QCOMPARE(highlighter.languageName(), QStringLiteral("JSON"));
    }

    // A named file still honours the size budget: recognition does not buy an
    // exemption from the performance guard.
    void sourceNameStillRespectsSizeBudget() {
        QString src;
        while (src.size() <= CodeHighlighter::maxHighlightChars())
            src += QStringLiteral("// filler comment line\n");

        QTextDocument doc;
        doc.setPlainText(src);
        CodeHighlighter highlighter(&doc);
        highlighter.setSourceName(QStringLiteral("big.cpp"));
        highlighter.setText(src);
        QCOMPARE(highlighter.languageName(), QString());
        QVERIFY(allFormats(doc).isEmpty());
    }

    // One enormous line defeats the line budget (it counts newlines), so it
    // gets its own guard: a 60KB minified blob must not run every rule across
    // the whole thing.
    void oversizedSingleLineStaysPlain() {
        QString src = QStringLiteral("{\"a\":\"");
        while (src.size() < 40 * 1024)
            src += QLatin1Char('x');
        src += QStringLiteral("\"}");

        QTextDocument doc;
        doc.setPlainText(src);
        CodeHighlighter highlighter(&doc);
        highlighter.setSourceName(QStringLiteral("min.json"));
        highlighter.setText(src);

        // The language is still recognised (the file is small and named)...
        QCOMPARE(highlighter.languageName(), QStringLiteral("JSON"));
        // ... but the single block is too long to be worth colouring.
        QVERIFY2(allFormats(doc).isEmpty(), "超长单行不应上色");

        // A short line of the same file does get coloured.
        QTextDocument small;
        const QString shortLine = QStringLiteral("{\"a\": \"value\", \"n\": 42}\n");
        small.setPlainText(shortLine);
        CodeHighlighter smallHighlighter(&small);
        smallHighlighter.setSourceName(QStringLiteral("min.json"));
        smallHighlighter.setText(shortLine);
        QVERIFY(!allFormats(small).isEmpty());
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
