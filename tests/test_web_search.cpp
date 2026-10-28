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
                                  QString* errorReason = nullptr,
                                  const std::atomic<bool>* = nullptr) const override {
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
                                  QString* errorReason = nullptr,
                                  const std::atomic<bool>* = nullptr) const override {
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
                                  QString* errorReason = nullptr,
                                  const std::atomic<bool>* = nullptr) const override {
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
        QVERIFY(def.readOnly);   // G1: retrieval stays available in plan mode
        QVERIFY(!def.parameters.isEmpty());
    }

    // L4: setBackend swaps the search backend at runtime.
    void setBackendSwapsBackend() {
        WebSearchTool tool; // default = 注册表默认后端（DuckDuckGo）
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

    // 服务来源（source）：每次调用都要报出「这次查询发给了谁」。
    // 成功与失败路径都必须带——失败时用户更需要知道是哪家失败。
    void resultCarriesProviderSource() {
        // 成功路径。
        WebSearchTool okTool(std::make_unique<FakeBackend>());
        okTool.setApiKey(QStringLiteral("k"));
        const ToolResult ok = okTool.execute({{"query", "qt"}});
        QVERIFY(ok.success);
        QCOMPARE(ok.source, QStringLiteral("fake"));

        // 失败路径（后端报错）。
        class FailingBackend : public WebSearchBackend {
        public:
            QString providerKey() const override { return QStringLiteral("failing"); }
            QList<WebSearchResult> search(const QString&, int, const QString&, int,
                                          QString* errorReason,
                                          const std::atomic<bool>*) const override {
                if (errorReason) *errorReason = QStringLiteral("后端不可用");
                return {};
            }
        };
        WebSearchTool failTool(std::make_unique<FailingBackend>());
        failTool.setApiKey(QStringLiteral("k"));
        const ToolResult fail = failTool.execute({{"query", "qt"}});
        QVERIFY(!fail.success);
        QCOMPARE(fail.source, QStringLiteral("failing"));
        QVERIFY(fail.error.contains(QStringLiteral("后端不可用")));

        // 缺密钥路径：还没发请求，但已经能确定「本该发给谁」。
        WebSearchTool noKeyTool(std::make_unique<FakeBackend>());
        const ToolResult noKey = noKeyTool.execute({{"query", "qt"}});
        QVERIFY(!noKey.success);
        QCOMPARE(noKey.source, QStringLiteral("fake"));

        // 本地工具没有外部来源：source 必须是空串（UI 据此隐藏徽章）。
        class LocalBackend : public WebSearchBackend {
        public:
            QString providerKey() const override { return QString(); }
            bool requiresApiKey() const override { return false; }
            QList<WebSearchResult> search(const QString&, int, const QString&, int,
                                          QString* errorReason,
                                          const std::atomic<bool>*) const override {
                if (errorReason) *errorReason = QString();
                WebSearchResult r;
                r.title = QStringLiteral("t");
                r.url = QStringLiteral("https://e.org");
                return {r};
            }
        };
        WebSearchTool localTool(std::make_unique<LocalBackend>());
        const ToolResult local = localTool.execute({{"query", "qt"}});
        QVERIFY(local.success);
        QVERIFY(local.source.isEmpty());
    }

    // ── 后端注册表（单一事实源）──

    // 注册表是 UI 下拉、schema options 与后端工厂的共同来源，三家必须
    // 一致，否则会出现「下拉里能选、工厂里没有」的幻影 provider。
    void registryKeysAreConsistent() {
        const QStringList keys = WebSearchProviderRegistry::keys();
        QCOMPARE(keys, (QStringList{QStringLiteral("duckduckgo"),
                                    QStringLiteral("zhipu"),
                                    QStringLiteral("deepseek")}));
        for (const QString& key : keys) {
            QVERIFY2(WebSearchProviderRegistry::find(key) != nullptr, qPrintable(key));
            auto backend = WebSearchProviderRegistry::create(key);
            QVERIFY2(backend != nullptr, qPrintable(key));
            // 描述符里的 key 必须与后端自报的 providerKey 一致。
            QCOMPARE(backend->providerKey(), key);
            QCOMPARE(backend->requiresApiKey(),
                     WebSearchProviderRegistry::requiresApiKey(key));
        }
        QVERIFY(WebSearchProviderRegistry::find(QStringLiteral("tavily")) == nullptr);
        QVERIFY(WebSearchProviderRegistry::find(QStringLiteral("searxng")) == nullptr);
        QVERIFY(WebSearchProviderRegistry::create(QStringLiteral("bogus")) == nullptr);
    }

    // 默认后端免密钥（首装即可用），且密钥桶只在需要密钥的后端上存在。
    void registryDefaultIsKeylessAndKeySlotsAreScoped() {
        const QString defaultKey = WebSearchProviderRegistry::defaultProviderKey();
        QCOMPARE(defaultKey, QStringLiteral("duckduckgo"));
        QVERIFY(!WebSearchProviderRegistry::requiresApiKey(defaultKey));
        QVERIFY(WebSearchProviderRegistry::apiKeySettingKey(defaultKey).isEmpty());

        // 需要密钥的后端各有一把独立的键——共用一个键会把 A 平台的密钥
        // 发给 B 平台。
        QCOMPARE(WebSearchProviderRegistry::apiKeySettingKey(QStringLiteral("zhipu")),
                 QStringLiteral("webSearch/zhipuApiKey"));
        QCOMPARE(WebSearchProviderRegistry::apiKeySettingKey(QStringLiteral("deepseek")),
                 QStringLiteral("webSearch/deepseekApiKey"));

        // 工具的默认构造也走注册表默认项，不硬编码某个具体后端。
        WebSearchTool tool;
        QCOMPARE(tool.definition().name, QStringLiteral("web_search"));
    }

    // 免密钥后端（duckduckgo）跳过 API key 检查，直接进入检索。
    void duckduckgoNeedsNoKey() {
        DuckDuckGoBackend backend;
        QCOMPARE(backend.providerKey(), QStringLiteral("duckduckgo"));
        QVERIFY(!backend.requiresApiKey());
        QVERIFY(backend.notReadyReason().isEmpty());
        // 空 query 是 no-op，不发起请求、也不报错。
        QVERIFY(backend.search(QString(), 5, QString(), 5000).isEmpty());
    }

    // 反爬挑战页识别：DDG 对疑似机器人返回 202 + anomaly 页面，页面里没有
    // 任何 result__a。识别不出来时零结果会被误报成「没找到结果」，用户只会
    // 反复改查询词。
    void duckduckgoDetectsAntiBotChallenge() {
        // 实测页面特征（202 + anomaly.js + challenge-form + 拼图 modal）。
        const QString challenge = QStringLiteral(
            "<form id=\"challenge-form\" action=\"//duckduckgo.com/anomaly.js\">"
            "<div class=\"anomaly-modal__description\">Please complete the "
            "following challenge</div>"
            "<div>Select all squares containing a duck:</div>");
        QVERIFY(DuckDuckGoBackend::looksLikeAntiBotChallenge(challenge));

        // 真实结果页不应被误判。
        const QString normal = QStringLiteral(
            "<a class=\"result__a\" href=\"https://example.com\">Example</a>"
            "<a class=\"result__snippet\">snippet</a>");
        QVERIFY(!DuckDuckGoBackend::looksLikeAntiBotChallenge(normal));
        QVERIFY(!DuckDuckGoBackend::looksLikeAntiBotChallenge(QString()));
    }

    // ── 智谱 Web Search ──

    // 请求构造：search_intent 是平台必填项（漏传直接 400）；query 不截断
    // （超长由 search() 提前拦截并给出可读原因，而不是静默改查询词）。
    void zhipuBuildsRequest() {
        const QJsonObject body = ZhipuSearchBackend::buildRequest(
            QStringLiteral("  qt 多线程  "), 3);
        QCOMPARE(body.value(QStringLiteral("search_query")).toString(),
                 QStringLiteral("qt 多线程"));
        QCOMPARE(body.value(QStringLiteral("search_engine")).toString(),
                 QStringLiteral("search_std"));
        QVERIFY(body.contains(QStringLiteral("search_intent")));
        QCOMPARE(body.value(QStringLiteral("search_intent")).toBool(), false);
        QCOMPARE(body.value(QStringLiteral("count")).toInt(), 3);
        // 截断规则与其它后端一致（1..10）。
        QCOMPARE(ZhipuSearchBackend::buildRequest(QStringLiteral("x"), 50)
                     .value(QStringLiteral("count")).toInt(), 10);
        QCOMPARE(ZhipuSearchBackend::buildRequest(QStringLiteral("x"), 0)
                     .value(QStringLiteral("count")).toInt(), 1);
    }

    // 响应解析：字段名是 link（不是 url）与 content；缺 url 丢弃、缺 title
    // 回退 url、标题去空白。
    void zhipuParsesResponse() {
        static const QByteArray kMock = QStringLiteral(
            "{"
            "\"search_result\": ["
            "  {\"title\": \"智谱文档\", \"link\": \"https://docs.bigmodel.cn/\","
            "   \"content\": \"搜索摘要\", \"media\": \"智谱\", \"publish_date\": \"2026-01-01\"},"
            "  {\"title\": \"  Qt 论坛  \", \"link\": \"https://forum.qt.io/\","
            "   \"content\": \"Qt 讨论\"},"
            "  {\"title\": \"没有链接\", \"content\": \"应被丢弃\"},"
            "  {\"link\": \"https://example.org/untitled\", \"content\": \"标题回退为链接\"}"
            "]"
            "}").toUtf8();

        QString reason;
        const QList<WebSearchResult> out =
            ZhipuSearchBackend::parseResponse(kMock, 10, &reason);
        QVERIFY(reason.isEmpty());
        QCOMPARE(out.size(), 3);
        QCOMPARE(out[0].title, QStringLiteral("智谱文档"));
        QCOMPARE(out[0].url, QStringLiteral("https://docs.bigmodel.cn/"));
        QCOMPARE(out[0].snippet, QStringLiteral("搜索摘要"));
        QCOMPARE(out[1].title, QStringLiteral("Qt 论坛"));
        QCOMPARE(out[2].title, out[2].url);
    }

    // 智谱的业务错误码要翻译成可操作的提示，而不是把原始 JSON 丢给用户。
    void zhipuMapsBusinessErrors() {
        static const QByteArray kBusy = QStringLiteral(
            "{\"error\":{\"code\":\"1701\",\"message\":\"并发超限\"}}").toUtf8();
        QString reason;
        QVERIFY(ZhipuSearchBackend::parseResponse(kBusy, 5, &reason).isEmpty());
        QVERIFY(reason.contains(QStringLiteral("并发")));

        static const QByteArray kAuth = QStringLiteral(
            "{\"error\":{\"code\":\"401\",\"message\":\"令牌已过期或验证不正确\"}}").toUtf8();
        QString authReason;
        QVERIFY(ZhipuSearchBackend::parseResponse(kAuth, 5, &authReason).isEmpty());
        QVERIFY(authReason.contains(QStringLiteral("令牌")));

        // 非对象 JSON：结构错误，不是「没搜到」。
        QString malformed;
        QVERIFY(ZhipuSearchBackend::parseResponse("[1,2,3]", 5, &malformed).isEmpty());
        QVERIFY(malformed.contains(QStringLiteral("JSON")));

        // 合法但零结果：空列表且不写原因（真的没搜到）。
        QString zero;
        QVERIFY(ZhipuSearchBackend::parseResponse("{\"search_result\": []}", 5, &zero).isEmpty());
        QVERIFY(zero.isEmpty());
    }

    // 缺 key / query 超 70 字符：必须在发请求前拦截并说明原因。
    void zhipuGuardsBeforeNetwork() {
        const ZhipuSearchBackend backend;
        QString noKey;
        QVERIFY(backend.search(QStringLiteral("hello"), 5, QString(), 5000, &noKey).isEmpty());
        QVERIFY(noKey.contains(QStringLiteral("智谱 API Key")));

        // 72 个字符已超平台 70 字符上限。
        const QString tooLong(72, QLatin1Char('x'));
        QString longReason;
        QVERIFY(backend.search(tooLong, 5, QStringLiteral("k"), 5000, &longReason).isEmpty());
        QVERIFY(longReason.contains(QString::number(ZhipuSearchBackend::kMaxQueryChars)));
        // 提示里要说明「当前多少个字符」，否则用户不知道要砍多少。
        QVERIFY(longReason.contains(QString::number(tooLong.size())));
    }

    // ── DeepSeek Search ──

    // 请求构造：need_content 必须为 false（content 是整页正文，会撑爆 6000
    // 字符预算；summary 才是给模型读的摘要）。
    void deepSeekBuildsRequest() {
        const QJsonObject body = DeepSeekSearchBackend::buildRequest(
            QStringLiteral("  DeepSeek V4 发布  "), 4);
        QCOMPARE(body.value(QStringLiteral("query")).toString(),
                 QStringLiteral("DeepSeek V4 发布"));
        QCOMPARE(body.value(QStringLiteral("count")).toInt(), 4);
        QVERIFY(body.contains(QStringLiteral("need_content")));
        QCOMPARE(body.value(QStringLiteral("need_content")).toBool(), false);
        QCOMPARE(DeepSeekSearchBackend::buildRequest(QStringLiteral("x"), 99)
                     .value(QStringLiteral("count")).toInt(), 10);
    }

    // 响应解析：summary 优先（长摘要，给模型读），回退 snippet；content
    // 即使存在也不进 snippet（那是整页正文）。
    void deepSeekParsesResponse() {
        static const QByteArray kMock = QStringLiteral(
            "{"
            "\"results\": ["
            "  {\"title\": \"DeepSeek V4\", \"url\": \"https://example.com/a\","
            "   \"snippet\": \"短摘要\", \"summary\": \"长摘要，适合给模型阅读\","
            "   \"content\": \"整页正文不应进入结果\","
            "   \"date\": \"2026-06-01T00:00:00+00:00\", \"authority\": 3},"
            "  {\"title\": \"只有 snippet\", \"url\": \"https://example.com/b\","
            "   \"snippet\": \"回退到这里\", \"summary\": \"\"},"
            "  {\"title\": \"无 url\", \"summary\": \"应被丢弃\"}"
            "]"
            "}").toUtf8();

        QString reason;
        const QList<WebSearchResult> out =
            DeepSeekSearchBackend::parseResponse(kMock, 10, &reason);
        QVERIFY(reason.isEmpty());
        QCOMPARE(out.size(), 2);
        QCOMPARE(out[0].snippet, QStringLiteral("长摘要，适合给模型阅读"));
        QVERIFY(!out[0].snippet.contains(QStringLiteral("整页正文")));
        QCOMPARE(out[1].snippet, QStringLiteral("回退到这里"));
    }

    // OpenAI 兼容错误信封原样透出；合法但零结果不报错。
    void deepSeekMapsErrors() {
        static const QByteArray kBalance = QStringLiteral(
            "{\"error\":{\"message\":\"Insufficient Balance\","
            "\"type\":\"invalid_request_error\",\"param\":null,"
            "\"code\":\"invalid_request_error\"}}").toUtf8();
        QString reason;
        QVERIFY(DeepSeekSearchBackend::parseResponse(kBalance, 5, &reason).isEmpty());
        QCOMPARE(reason, QStringLiteral("Insufficient Balance"));

        // 正常响应：解析出结果且无原因。
        static const QByteArray kOk = QStringLiteral(
            "{\"results\":[{\"title\":\"t\",\"url\":\"https://e.org\","
            "\"summary\":\"s\"}]}").toUtf8();
        QString okReason;
        QCOMPARE(DeepSeekSearchBackend::parseResponse(kOk, 5, &okReason).size(), 1);
        QVERIFY(okReason.isEmpty());

        // 官方约定：200 + 空数组是「没搜到」，且不计费。
        QString zero;
        QVERIFY(DeepSeekSearchBackend::parseResponse("{\"results\": []}", 5, &zero).isEmpty());
        QVERIFY(zero.isEmpty());
    }

    void deepSeekGuardsBeforeNetwork() {
        const DeepSeekSearchBackend backend;
        QString noKey;
        QVERIFY(backend.search(QStringLiteral("hello"), 5, QString(), 5000, &noKey).isEmpty());
        QVERIFY(noKey.contains(QStringLiteral("DeepSeek API Key")));
        QVERIFY(backend.search(QString(), 5, QStringLiteral("k"), 5000).isEmpty());
    }
};

QTEST_MAIN(TestWebSearch)
#include "test_web_search.moc"
