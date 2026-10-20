// P3-a: the commit-message drafter's pure contract. No network and no settings
// are touched: prompt assembly, diff truncation, reply cleaning and the
// local-only gate are static functions, and the empty-stage refusal happens
// before any configuration is read.

#include "viewmodels/CommitMessageDrafter.h"

#include <QSignalSpy>
#include <QtTest>

using namespace NexusAI;

class TestCommitDrafter : public QObject {
    Q_OBJECT

private slots:
    void blockedByLocalOnlyTable() {
        // Local-only mode is the user's "content stays on this machine" switch,
        // so a cloud profile is blocked and a local profile is allowed.
        QVERIFY(CommitMessageDrafter::blockedByLocalOnly(true, false));
        QVERIFY(!CommitMessageDrafter::blockedByLocalOnly(true, true));
        QVERIFY(!CommitMessageDrafter::blockedByLocalOnly(false, false));
        QVERIFY(!CommitMessageDrafter::blockedByLocalOnly(false, true));
    }

    void truncateDiffCutsOnLineBoundary() {
        QByteArray lines;
        for (int i = 0; i < 400; ++i)
            lines += QStringLiteral("line %1 content padding\n").arg(i).toUtf8();
        const QString diff = QString::fromUtf8(lines);

        const QString shortDiff = QStringLiteral("a\nb\n");
        QCOMPARE(CommitMessageDrafter::truncateDiff(shortDiff, 1024), shortDiff);

        const QString cut = CommitMessageDrafter::truncateDiff(diff, 500);
        QVERIFY2(cut.contains(QStringLiteral("…[差异已截断")), qPrintable(cut.right(80)));
        // Never hand the model half a line: the kept part ends on a newline.
        const int marker = cut.indexOf(QStringLiteral("…[差异已截断"));
        QVERIFY(marker > 0);
        QVERIFY2(cut.at(marker - 1) == QLatin1Char('\n'),
                 "the cut must land on a line boundary");
        QVERIFY2(cut.left(marker).contains(QStringLiteral("line 0 content")),
                 qPrintable(cut.left(60)));
        QVERIFY(!cut.left(marker).contains(QStringLiteral("line 399")));
    }

    void buildPromptAssemblesSubjectsAndDiff() {
        QStringList subjects;
        for (int i = 0; i < 8; ++i)
            subjects << QStringLiteral("subject %1").arg(i);
        const QString diff = QStringLiteral("diff --git a/x b/x\n+hello\n");

        const QString prompt = CommitMessageDrafter::buildPrompt(diff, subjects);
        QVERIFY(prompt.contains(QStringLiteral("subject 0")));
        QVERIFY(prompt.contains(QStringLiteral("subject 4")));
        QVERIFY2(!prompt.contains(QStringLiteral("subject 5")),
                 "only the five most recent subjects may be sent");
        QVERIFY(prompt.contains(diff.trimmed()));
        QVERIFY(prompt.contains(QStringLiteral("已暂存差异：")));

        const QString bare = CommitMessageDrafter::buildPrompt(diff, {});
        QVERIFY2(!bare.contains(QStringLiteral("参考最近提交主题")),
                 "an empty subject list must not add the reference block");
        QVERIFY(bare.contains(diff.trimmed()));
    }

    void cleanDraftStripsModelNoise() {
        // Fenced answer.
        QCOMPARE(CommitMessageDrafter::cleanDraft(
                     QStringLiteral("```\nfeat(git): 支持分块暂存\n\n正文说明\n```")),
                 QStringLiteral("feat(git): 支持分块暂存\n\n正文说明"));
        // Quoted one-liner.
        QCOMPARE(CommitMessageDrafter::cleanDraft(
                     QStringLiteral("   \"fix(git): 修正索引刷新\"   ")),
                 QStringLiteral("fix(git): 修正索引刷新"));
        // Stray bullet and a label before the subject.
        QCOMPARE(CommitMessageDrafter::cleanDraft(
                     QStringLiteral("- feat: 主题")),
                 QStringLiteral("feat: 主题"));
        QCOMPARE(CommitMessageDrafter::cleanDraft(
                     QStringLiteral("提交说明：feat: 主题")),
                 QStringLiteral("feat: 主题"));
        // Blank runs collapse, both ends are trimmed.
        QCOMPARE(CommitMessageDrafter::cleanDraft(
                     QStringLiteral("\n\nfeat: a\n\n\n\nbody\n\n\n")),
                 QStringLiteral("feat: a\n\nbody"));
        // A clean answer is untouched.
        QCOMPARE(CommitMessageDrafter::cleanDraft(QStringLiteral("docs: 更新说明")),
                 QStringLiteral("docs: 更新说明"));
        QCOMPARE(CommitMessageDrafter::cleanDraft(QStringLiteral("   \n  \n")),
                 QString());
    }

    void cleanDraftCapsBodyLength() {
        QStringList lines;
        lines << QStringLiteral("feat: 主题");
        lines << QString();
        for (int i = 0; i < 200; ++i)
            lines << QStringLiteral("body line %1").arg(i);
        const QString cleaned = CommitMessageDrafter::cleanDraft(
            lines.join(QLatin1Char('\n')));
        int content = 0;
        for (const QString& line : cleaned.split(QLatin1Char('\n')))
            if (!line.trimmed().isEmpty()) ++content;
        QVERIFY2(content <= CommitMessageDrafter::kMaxBodyLines,
                 qPrintable(QStringLiteral("kept %1 content lines").arg(content)));
        QVERIFY(cleaned.startsWith(QStringLiteral("feat: 主题")));
    }

    void draftRefusesWithoutStagedContent() {
        CommitMessageDrafter drafter;
        QSignalSpy spy(&drafter, &CommitMessageDrafter::drafted);
        drafter.draft(QStringLiteral("   \n"), {});
        QCOMPARE(spy.count(), 1);
        const QList<QVariant> args = spy.takeFirst();
        QVERIFY2(!args.at(0).toBool(), "an empty stage must be refused");
        QVERIFY(!args.at(2).toString().isEmpty());
        QVERIFY(args.at(1).toString().isEmpty());
        // Nothing may be left holding the single-request gate.
        QVERIFY(!drafter.busy());
        // Cancel without a running request is a no-op, not a second signal.
        drafter.cancel();
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_GUILESS_MAIN(TestCommitDrafter)
#include "test_commit_drafter.moc"
