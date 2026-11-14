// docs/53: MathText — lightweight LaTeX → Unicode approximation for the chat
// render path. Conversion matrix + guards (no-math spans, `code` spans,
// unknown commands verbatim).
#include "core/utils/MathText.h"

#include <QtTest>

using namespace NexusAI::MathText;

class TestMathText : public QObject {
    Q_OBJECT

private slots:
    void greekAndOperatorsConvert() {
        QCOMPARE(render(QStringLiteral("$\\alpha \\times \\beta \\geq \\gamma$")),
                 QStringLiteral("α × β ≥ γ"));
        // TeX source carries no space between the two scripts — neither does
        // the output.
        QCOMPARE(render(QStringLiteral("$\\sum_{i=1}^{n} x_i$")),
                 QStringLiteral("∑ᵢ₌₁ⁿ xᵢ"));
    }

    void scriptsMapToUnicodeChars() {
        QCOMPARE(render(QStringLiteral("$x^2 + y_1$")),
                 QStringLiteral("x² + y₁"));
        // Exponent groups map char by char.
        QCOMPARE(render(QStringLiteral("$2^{10}$")), QStringLiteral("2¹⁰"));
        // Unmappable script chars keep the TeX form verbatim.
        QCOMPARE(render(QStringLiteral("$x^{\\alpha}$")),
                 QStringLiteral("x^α"));
    }

    void fracAndSqrtApproximate() {
        QCOMPARE(render(QStringLiteral("$\\frac{a}{b}$")),
                 QStringLiteral("(a)/(b)"));
        QCOMPARE(render(QStringLiteral("$\\sqrt{x}$")), QStringLiteral("√(x)"));
        QCOMPARE(render(QStringLiteral("$\\sqrt[3]{8}$")), QStringLiteral("³√(8)"));
    }

    void displaySpansGetLinePadding() {
        QCOMPARE(render(QStringLiteral("before $$a+b$$ after")),
                 QStringLiteral("before \na+b\n after"));
        QCOMPARE(render(QStringLiteral("x \\[y = 1\\] z")),
                 QStringLiteral("x \ny = 1\n z"));
    }

    void nonMathDollarSpansStayUntouched() {
        // No math markers inside → the span must survive verbatim.
        QCOMPARE(render(QStringLiteral("the price is $5 and $10 total")),
                 QStringLiteral("the price is $5 and $10 total"));
    }

    void inlineCodeSpansAreProtected() {
        QCOMPARE(render(QStringLiteral("run `$x^2$` first")),
                 QStringLiteral("run `$x^2$` first"));
    }

    void unknownCommandsStayVerbatim() {
        QCOMPARE(render(QStringLiteral("$\\foo{x}$")), QStringLiteral("\\foo{x}"));
    }

    void shellCommandsAndEnvironmentsDeShell() {
        QCOMPARE(render(QStringLiteral("$\\left(a\\right)$")),
                 QStringLiteral("(a)"));
        // \begin/\end shells drop; \\ becomes a line break; & becomes two
        // spaces (plus the spaces already around it in the source).
        QCOMPARE(render(QStringLiteral("$$\\begin{matrix}a & b\\\\c & d\\end{matrix}$$")),
                 QStringLiteral("\na    b\nc    d\n"));
        QCOMPARE(render(QStringLiteral("$\\text{speed} = v$")),
                 QStringLiteral("speed = v"));
    }

    void plainTextPassesThrough() {
        const QString text = QStringLiteral("普通文本，无公式，保留原样 100%。");
        QCOMPARE(render(text), text);
    }
};

QTEST_GUILESS_MAIN(TestMathText)
#include "test_math_text.moc"
