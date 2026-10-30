// 用量页「导出 CSV」回归。
//
// 导出的是表格当前视图（筛选 + 排序后的行），写成 Excel 能直接打开的
// CSV。容易出错的三处锁在这里：
//   1. UTF-8 BOM + CRLF —— 否则中文表头在 Excel 里乱码 / 串行；
//   2. RFC 4180 转义 —— 模型名或会话 ID 里的逗号、引号；
//   3. 公式注入防护 —— = + - @ 开头的单元格前置单引号，避免被当公式执行。
#include "viewmodels/AgentViewModel.h"

#include <QDir>
#include <QFile>
#include <QtTest>

using namespace NexusAI;

class TestUsageExport : public QObject {
    Q_OBJECT
private slots:
    void writesBomHeaderAndEscapedRows() {
        AgentViewModel vm(nullptr);
        const QString path = QDir::current().filePath(
            QStringLiteral("usage-export-test.csv"));
        QFile::remove(path);

        QVariantList rows;
        rows.append(QVariantMap{
            {QStringLiteral("timestamp"), 1700000000000LL},
            {QStringLiteral("model"), QStringLiteral("gpt-4o")},
            {QStringLiteral("inputTokens"), 1200},
            {QStringLiteral("outputTokens"), 345},
            {QStringLiteral("cacheHitTokens"), 900},
            {QStringLiteral("latencyMs"), 1234},
            {QStringLiteral("costCents"), 42},
            {QStringLiteral("sessionId"), QStringLiteral("s-1")},
            {QStringLiteral("requestId"), QStringLiteral("req-1")}});
        rows.append(QVariantMap{
            {QStringLiteral("timestamp"), 1700000060000LL},
            {QStringLiteral("model"), QStringLiteral("qwen,\"plus\"")},
            {QStringLiteral("inputTokens"), 1},
            {QStringLiteral("outputTokens"), 2},
            {QStringLiteral("cacheHitTokens"), 0},
            {QStringLiteral("latencyMs"), 5},
            {QStringLiteral("costCents"), 0},
            {QStringLiteral("sessionId"), QStringLiteral("s,2")},
            {QStringLiteral("requestId"), QStringLiteral("=cmd|' /C calc'!A0")}});

        QVERIFY(vm.exportUsageCsv(path, rows));

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray data = file.readAll();
        file.close();
        QFile::remove(path);

        QVERIFY(data.startsWith(QByteArray("\xEF\xBB\xBF", 3)));
        const QString text = QString::fromUtf8(data.mid(3));
        const QStringList lines = text.split(QStringLiteral("\r\n"),
                                             Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines.at(0),
                 QStringLiteral("时间,模型,输入 Tokens,输出 Tokens,"
                                "缓存命中 Tokens,延迟(ms),费用(元),"
                                "会话 ID,请求 ID"));
        // 时间列按本地时区渲染，这里只校验其后的数值列与顺序。
        QVERIFY(lines.at(1).endsWith(
            QStringLiteral(",gpt-4o,1200,345,900,1234,0.4200,s-1,req-1")));
        // 逗号 / 引号走 RFC 4180；= 开头的请求 ID 前置单引号。
        QVERIFY(lines.at(2).contains(QStringLiteral("\"qwen,\"\"plus\"\"\"")));
        QVERIFY(lines.at(2).contains(QStringLiteral("'=cmd|' /C calc'!A0")));
    }

    void rejectsEmptyRows() {
        AgentViewModel vm(nullptr);
        QString error;
        connect(&vm, &AgentViewModel::errorOccurred, this,
                [&error](const QString& message) { error = message; });
        QVERIFY(!vm.exportUsageCsv(
            QDir::current().filePath(QStringLiteral("usage-empty.csv")), {}));
        QVERIFY(!error.isEmpty());
    }

    void rejectsUnwritablePath() {
        AgentViewModel vm(nullptr);
        QString error;
        connect(&vm, &AgentViewModel::errorOccurred, this,
                [&error](const QString& message) { error = message; });
        QVariantList rows;
        rows.append(QVariantMap{{QStringLiteral("timestamp"), 1LL},
                                {QStringLiteral("model"), QStringLiteral("m")}});
        QVERIFY(!vm.exportUsageCsv(
            QDir::current().filePath(
                QStringLiteral("no-such-dir/usage.csv")),
            rows));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestUsageExport)
#include "test_usage_export.moc"
