// T6 web_search tool unit tests.
#include "core/tools/WebSearchTool.h"
#include "core/tools/WebSearchBackend.h"

#include <QtTest>

using namespace NexusAI;

class FakeBackend : public WebSearchBackend {
public:
    QString providerKey() const override { return QStringLiteral("fake"); }

    QList<WebSearchResult> search(const QString& query, int maxResults,
                                  const QString&, int) const override {
        QList<WebSearchResult> out;
        if (query.isEmpty()) return out;
        maxResults = qBound(1, maxResults, 10);
        for (int i = 0; i < maxResults; ++i) {
            WebSearchResult r;
            r.title = QStringLiteral("Result %1 for %2").arg(i + 1).arg(query);
            r.url = QStringLiteral("https://example.com/%1").arg(i);
            r.snippet = QStringLiteral("Snippet %1").arg(i);
            out.append(r);
        }
        return out;
    }
};

class TestWebSearch : public QObject {
    Q_OBJECT
private slots:
    void missingApiKeyFails() {
        auto backend = std::make_unique<FakeBackend>();
        WebSearchTool tool(std::move(backend));
        tool.setApiKey(QString());
        const ToolResult r = tool.execute({{"query", "hello"}});
        QVERIFY(!r.success);
        QVERIFY(r.error.contains(QStringLiteral("未配置")));
    }

    void queryRequired() {
        WebSearchTool tool(std::make_unique<FakeBackend>());
        tool.setApiKey(QStringLiteral("dummy-key"));
        {
            const ToolResult r = tool.execute({{"query", ""}});
            QVERIFY(!r.success);
            QVERIFY(r.error.contains(QStringLiteral("query is required")));
        }
        {
            const ToolResult r = tool.execute({});
            QVERIFY(!r.success);
        }
    }

    void returnsFormattedResults() {
        WebSearchTool tool(std::make_unique<FakeBackend>());
        tool.setApiKey(QStringLiteral("dummy-key"));
        const ToolResult r = tool.execute({{"query", "qt"}, {"max_results", 3}});
        QVERIFY(r.success);
        QVERIFY(r.output.contains(QStringLiteral("qt")));
        QVERIFY(r.output.contains(QStringLiteral("example.com")));
    }

    void resultCap() {
        WebSearchTool tool(std::make_unique<FakeBackend>());
        tool.setApiKey(QStringLiteral("dummy-key"));
        const ToolResult r = tool.execute({{"query", "test"}, {"max_results", 100}});
        QVERIFY(r.success);
        QVERIFY(r.output.size() <= WebSearchTool::kMaxResultChars + 200);
    }

    void cancelIsNoop() {
        WebSearchTool tool(std::make_unique<FakeBackend>());
        tool.setApiKey(QStringLiteral("dummy-key"));
        tool.cancel();
        const ToolResult r = tool.execute({{"query", "hello"}});
        QVERIFY(r.success);
    }

    void definitionHasConfirmLevel() {
        WebSearchTool tool(std::make_unique<FakeBackend>());
        const ToolDefinition def = tool.definition();
        QCOMPARE(def.name, QStringLiteral("web_search"));
        QCOMPARE(def.level, ToolLevel::RequiresConfirm);
        QVERIFY(!def.parameters.isEmpty());
    }
};

QTEST_MAIN(TestWebSearch)
#include "test_web_search.moc"
