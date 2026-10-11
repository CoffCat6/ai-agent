// T6 web_search tool unit tests.
#include "core/tools/WebSearchTool.h"
#include "core/tools/WebSearchBackend.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>

#include <QtTest>

using namespace NexusAI;

class FakeBackend : public WebSearchBackend {
public:
    QString providerKey() const override { return QStringLiteral("fake"); }

    QList<WebSearchResult> search(const QString& query, int maxResults,
                                  const QString&, int,
                                  QString* errorReason = nullptr) const override {
        if (errorReason) *errorReason = QString();
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

class FakeNoKeyBackend : public WebSearchBackend {
public:
    QString providerKey() const override { return QStringLiteral("fake_nokey"); }
    bool requiresApiKey() const override { return false; }

    QList<WebSearchResult> search(const QString& query, int,
                                  const QString&, int,
                                  QString* errorReason = nullptr) const override {
        if (errorReason) *errorReason = QString();
        QList<WebSearchResult> out;
        if (query.isEmpty()) return out;
        WebSearchResult r;
        r.title = query;
        r.url = QStringLiteral("https://example.com/ddg");
        r.snippet = QStringLiteral("no-key result");
        out.append(r);
        return out;
    }
};

// docs/22 F3: holds the search call open long enough for the test to swap
// the backend while execute() is inside search().
class SlowFakeBackend : public WebSearchBackend {
public:
    QString providerKey() const override { return QStringLiteral("slow"); }

    QList<WebSearchResult> search(const QString& query, int,
                                  const QString&, int,
                                  QString* errorReason = nullptr) const override {
        if (errorReason) *errorReason = QString();
        QTest::qSleep(200);
        QList<WebSearchResult> out;
        WebSearchResult r;
        r.title = QStringLiteral("slow:%1").arg(query);
        r.url = QStringLiteral("https://slow.example.com");
        r.snippet = QStringLiteral("held open");
        out.append(r);
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

    // L4: setBackend swaps the search backend at runtime.
    void setBackendSwapsBackend() {
        WebSearchTool tool; // default = TavilyBackend
        tool.setApiKey(QStringLiteral("dummy-key"));
        tool.setBackend(std::make_unique<FakeBackend>());
        const ToolResult r = tool.execute({{"query", "test"}, {"max_results", 2}});
        QVERIFY(r.success);
        QVERIFY(r.output.contains(QStringLiteral("example.com")));
    }

    // docs/22 F3: swapping the backend mid-flight must not disturb the
    // running call — execute() works on a config snapshot, the next call
    // picks up the new backend.
    void setBackendWhileInFlightIsSafe() {
        WebSearchTool tool(std::make_unique<SlowFakeBackend>());
        tool.setApiKey(QStringLiteral("dummy-key"));

        QThread* worker = QThread::create([&tool]() {
            const ToolResult r = tool.execute({{"query", "old"}});
            QVERIFY(r.success);
            QVERIFY(r.output.contains(QStringLiteral("slow:old")));
        });
        QObject::connect(worker, &QThread::finished, worker,
                         &QThread::deleteLater);
        worker->start();

        QTest::qWait(60);   // in-flight, inside SlowFakeBackend::search
        tool.setBackend(std::make_unique<FakeBackend>());
        QVERIFY(QTest::qWaitFor([worker]() { return worker->isFinished(); },
                                8000));

        // The next call runs on the swapped backend.
        const ToolResult next = tool.execute({{"query", "new"}, {"max_results", 1}});
        QVERIFY(next.success);
        QVERIFY(next.output.contains(QStringLiteral("Result 1 for new")));
    }

    // L4: setBackend(nullptr) is ignored — the tool keeps its current backend.
    void setBackendNullptrIsIgnored() {
        WebSearchTool tool(std::make_unique<FakeBackend>());
        tool.setApiKey(QStringLiteral("dummy-key"));
        tool.setBackend(nullptr);
        const ToolResult r = tool.execute({{"query", "test"}, {"max_results", 1}});
        QVERIFY(r.success);
        QVERIFY(r.output.contains(QStringLiteral("example.com")));
    }

    // DuckDuckGo backend: providerKey and requiresApiKey properties.
    void duckduckgoBackendProperties() {
        DuckDuckGoBackend backend;
        QCOMPARE(backend.providerKey(), QStringLiteral("duckduckgo"));
        QVERIFY(!backend.requiresApiKey());
    }

    // Backends with requiresApiKey()=false skip the API key check.
    void noKeyBackendSkipsApiKeyCheck() {
        WebSearchTool tool(std::make_unique<FakeNoKeyBackend>());
        tool.setApiKey(QString());
        const ToolResult r = tool.execute({{"query", "hello"}});
        QVERIFY(r.success);
        QVERIFY(r.output.contains(QStringLiteral("example.com")));
    }

    // ── docs/20 §5.3 (C3): SearXNG backend ──

    // providerKey / requiresApiKey / baseUrl wiring (no API key by design).
    void searxBackendProperties() {
        SearxBackend backend(QStringLiteral("  http://localhost:8080/  "));
        QCOMPARE(backend.providerKey(), QStringLiteral("searxng"));
        QVERIFY(!backend.requiresApiKey());
        QCOMPARE(backend.baseUrl(), QStringLiteral("http://localhost:8080/"));
        // Default-constructed backend has no instance configured.
        QVERIFY(SearxBackend().baseUrl().isEmpty());
    }

    // URL building: <baseUrl>/search?q=…&format=json — trailing slash is
    // normalized, a base path is preserved, and the query round-trips
    // through percent-encoding.
    void searxngBuildsSearchUrl() {
        const QUrl url = SearxBackend::buildSearchUrl(
            QStringLiteral("http://localhost:8080/"), QStringLiteral("qt & widgets"));
        QVERIFY(url.isValid());
        QVERIFY(!url.isRelative());
        QCOMPARE(url.scheme(), QStringLiteral("http"));
        QCOMPARE(url.host(), QStringLiteral("localhost"));
        QCOMPARE(url.port(), 8080);
        QCOMPARE(url.path(), QStringLiteral("/search"));

        const QUrlQuery q(url.query());
        QCOMPARE(q.queryItemValue(QStringLiteral("q"), QUrl::FullyDecoded),
                 QStringLiteral("qt & widgets"));
        QCOMPARE(q.queryItemValue(QStringLiteral("format")), QStringLiteral("json"));

        // A deployment under a sub-path keeps it: …/searx → …/searx/search.
        QCOMPARE(SearxBackend::buildSearchUrl(QStringLiteral("http://host/searx/"),
                                              QStringLiteral("x")).path(),
                 QStringLiteral("/searx/search"));
    }

    // Response parsing: {title,url,content} mapping identical to Tavily —
    // entries without a URL are dropped, a missing title falls back to the
    // URL, titles are trimmed.
    // (Plain escaped literals instead of R"()" raw strings: moc cannot
    // lex raw string literals and would emit an empty metaobject.)
    void searxngParsesMockJson() {
        static const QByteArray kMock = QStringLiteral(
            "{"
            "\"query\": \"qt searxng\","
            "\"number_of_results\": 4,"
            "\"results\": ["
            "  {\"title\": \"SearXNG docs\", \"url\": \"https://docs.searxng.org/\","
            "   \"content\": \"Privacy-focused metasearch engine\"},"
            "  {\"title\": \"  Qt Forum  \", \"url\": \"https://forum.qt.io/\","
            "   \"content\": \"Qt discussion\"},"
            "  {\"title\": \"Dropped: no url\", \"content\": \"should never appear\"},"
            "  {\"url\": \"https://example.org/untitled\","
            "   \"content\": \"title falls back to url\"}"
            "]"
            "}").toUtf8();

        const QList<WebSearchResult> out = SearxBackend::parseResponse(kMock, 10);
        QCOMPARE(out.size(), 3);
        QCOMPARE(out[0].title, QStringLiteral("SearXNG docs"));
        QCOMPARE(out[0].url, QStringLiteral("https://docs.searxng.org/"));
        QCOMPARE(out[0].snippet, QStringLiteral("Privacy-focused metasearch engine"));
        QCOMPARE(out[1].title, QStringLiteral("Qt Forum"));
        QCOMPARE(out[2].url, QStringLiteral("https://example.org/untitled"));
        QCOMPARE(out[2].title, out[2].url);
    }

    // maxResults follows the Tavily truncation rule: qBound(1, n, 10) —
    // the parse caps at 10 even when the caller asks for more.
    void searxngParseCapsMaxResults() {
        QJsonArray results;
        for (int i = 0; i < 15; ++i) {
            results.append(QJsonObject{
                {QStringLiteral("title"), QStringLiteral("R%1").arg(i)},
                {QStringLiteral("url"), QStringLiteral("https://example.com/%1").arg(i)},
                {QStringLiteral("content"), QStringLiteral("snippet %1").arg(i)}});
        }
        const QByteArray json = QJsonDocument(QJsonObject{{QStringLiteral("results"), results}})
                                    .toJson(QJsonDocument::Compact);

        QCOMPARE(SearxBackend::parseResponse(json, 100).size(), 10);
        QCOMPARE(SearxBackend::parseResponse(json, 3).size(), 3);
        // qBound lower edge: 0 clamps to 1.
        QCOMPARE(SearxBackend::parseResponse(json, 0).size(), 1);
    }

    // Malformed payloads degrade to an empty result list (the tool layer
    // turns that into a user-facing failure), never a crash.
    void searxngInvalidPayloadReturnsEmpty() {
        QVERIFY(SearxBackend::parseResponse("not json at all", 5).isEmpty());
        QVERIFY(SearxBackend::parseResponse("[1,2,3]", 5).isEmpty());
        QVERIFY(SearxBackend::parseResponse("{\"results\": []}", 5).isEmpty());
        QVERIFY(SearxBackend::parseResponse(
                    "{\"results\": [{\"title\": \"no url\"}, \"junk\"]}", 5).isEmpty());
    }

    // Guard conditions short-circuit before any network I/O: empty query or
    // unconfigured baseUrl ("" from webSearch/searxBaseUrl) returns {}.
    void searxngSearchWithoutBaseUrlOrQueryIsNoop() {
        const SearxBackend unconfigured;
        QVERIFY(unconfigured.search(QStringLiteral("hello"), 5, QString(), 5000).isEmpty());

        const SearxBackend configured(QStringLiteral("http://localhost:8080"));
        QVERIFY(configured.search(QString(), 5, QString(), 5000).isEmpty());
        QVERIFY(configured.search(QStringLiteral("   "), 5, QString(), 5000).isEmpty());
    }

    // ── docs/20 §5.3 (code-review fix): 可操作的错误诊断 ──

    // 未配置 baseUrl 必须报「去填地址」，而不是让工具层说「请检查网络连接」。
    void searxngNotReadyReasonNamesTheSetting() {
        QVERIFY(SearxBackend().notReadyReason().isEmpty() == false);
        QVERIFY(SearxBackend().notReadyReason().contains(
            QStringLiteral("webSearch/searxBaseUrl")));
        // 配好地址后就绪（空串 = 就绪）。
        QVERIFY(SearxBackend(QStringLiteral("http://localhost:8080")).notReadyReason().isEmpty());
    }

    // search() 的 errorReason out-param 把「没搜到」与「请求失败」分开：
    // 缺 baseUrl 时原因必须指明是未配置，而不是超时/网络。
    void searxngSearchReportsConfigurationFailure() {
        const SearxBackend unconfigured;
        QString reason;
        QVERIFY(unconfigured.search(QStringLiteral("hello"), 5, QString(), 5000, &reason).isEmpty());
        QVERIFY(!reason.isEmpty());
        QVERIFY(reason.contains(QStringLiteral("webSearch/searxBaseUrl")));

        // 已配置 + 空查询：仍然空结果，但不该报成「未配置」。
        const SearxBackend configured(QStringLiteral("http://localhost:8080"));
        QString noopReason = QStringLiteral("sentinel");
        QVERIFY(configured.search(QString(), 5, QString(), 5000, &noopReason).isEmpty());
        QCOMPARE(noopReason, QStringLiteral("sentinel"));
    }

    // 非法地址（无 scheme / scheme 不是 http/https）也要给出可读原因，
    // 而不是静默空结果。
    void searxngSearchReportsInvalidBaseUrl() {
        const SearxBackend bad(QStringLiteral("localhost:8080"));
        QString reason;
        QVERIFY(bad.search(QStringLiteral("hello"), 5, QString(), 5000, &reason).isEmpty());
        QVERIFY(!reason.isEmpty());
        QVERIFY(reason.contains(QStringLiteral("localhost:8080")));
        // 提示里要点明可用的 scheme。
        QVERIFY(reason.contains(QStringLiteral("http://")));
    }

    // parseResponse 的既有契约不变：合法 JSON 但零结果 → 空列表且无原因
    // （真的没搜到）；非对象 JSON → 空列表（工具层据此判定响应结构不对）。
    void searxngParseFailureIsDistinctFromZeroResults() {
        const QList<WebSearchResult> zeroResults =
            SearxBackend::parseResponse(QStringLiteral("{\"results\": []}").toUtf8(), 5);
        QVERIFY(zeroResults.isEmpty());
        const QList<WebSearchResult> malformed =
            SearxBackend::parseResponse(QStringLiteral("[1,2,3]").toUtf8(), 5);
        QVERIFY(malformed.isEmpty());
        // 有结果的响应仍照常解析（回归保护）。
        const QList<WebSearchResult> one =
            SearxBackend::parseResponse(
                QStringLiteral("{\"results\":[{\"url\":\"https://e.org\"}]}").toUtf8(), 5);
        QCOMPARE(one.size(), 1);
        QCOMPARE(one[0].url, QStringLiteral("https://e.org"));
    }
};

QTEST_MAIN(TestWebSearch)
#include "test_web_search.moc"
