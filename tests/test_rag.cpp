// RagService: multi-document ingest/search/remove with the offline
// pseudo-embedding fallback, and the KnowledgeSearchTool surface.
#include "core/rag/RagService.h"
#include "core/rag/KnowledgeSearchTool.h"

#include <QtTest>

using namespace NexusAI;

namespace {

// >512 chars so the default chunker produces several chunks
QString longDoc(const QString& tag, int copies) {
    QString text;
    const QString paragraph = QStringLiteral(
        "%1 alpha beta gamma delta epsilon zeta eta theta iota kappa "
        "lambda mu nu xi omicron pi rho sigma tau upsilon phi chi psi omega. ").arg(tag);
    for (int i = 0; i < copies; ++i) text += paragraph;
    return text;
}

} // namespace

class TestRag : public QObject {
    Q_OBJECT

private slots:
    void ingestAndListDocuments();
    void searchReturnsScoredChunks();
    void removeDocument();
    void replaceSameName();
    void emptyInputRejected();
    void toolOnEmptyBase();
    void toolSearchResult();

private:
    RagService rag;
};

void TestRag::ingestAndListDocuments() {
    const int chunks = rag.ingestDocument("doc1.txt", longDoc("one", 10));
    QVERIFY(chunks >= 2); // chunked into several passages

    const QJsonArray docs = rag.documents();
    QCOMPARE(docs.size(), 1);
    QCOMPARE(docs.at(0).toObject().value("name").toString(), QStringLiteral("doc1.txt"));
    QVERIFY(docs.at(0).toObject().value("chunks").toInt() >= 2);
}

void TestRag::searchReturnsScoredChunks() {
    rag.ingestDocument("doc2.txt", longDoc("two", 8));
    const QJsonArray hits = rag.search("alpha beta", 4);
    QVERIFY(!hits.isEmpty());
    QVERIFY(hits.size() <= 4);
    for (const auto& entry : hits) {
        const QJsonObject hit = entry.toObject();
        QVERIFY(hit.contains("score"));
        QVERIFY(hit.value("source").toString() == "doc1.txt"
             || hit.value("source").toString() == "doc2.txt");
        QVERIFY(!hit.value("text").toString().isEmpty());
    }
}

void TestRag::removeDocument() {
    QVERIFY(rag.removeDocument("doc1.txt"));
    const QJsonArray docs = rag.documents();
    QCOMPARE(docs.size(), 1);
    QCOMPARE(docs.at(0).toObject().value("name").toString(), QStringLiteral("doc2.txt"));
    for (const auto& entry : rag.search("alpha", 10)) {
        QCOMPARE(entry.toObject().value("source").toString(), QStringLiteral("doc2.txt"));
    }
    QVERIFY(!rag.removeDocument("doc1.txt")); // already gone
}

void TestRag::replaceSameName() {
    const int before = rag.documentCount();
    QVERIFY(before > 0);
    rag.ingestDocument("doc2.txt", longDoc("two-again", 8));
    const QJsonArray docs = rag.documents();
    QCOMPARE(docs.size(), 1); // replaced, not duplicated
}

void TestRag::emptyInputRejected() {
    QCOMPARE(rag.ingestDocument("empty.txt", "   "), 0);
    QCOMPARE(rag.ingestDocument("", "content"), 0);
}

void TestRag::toolOnEmptyBase() {
    RagService emptyRag;
    KnowledgeSearchTool tool(&emptyRag);
    const ToolResult r = tool.execute(QJsonObject{{"query", "anything"}});
    QVERIFY(r.success);
    QVERIFY(r.output.contains("empty"));

    // the shared rag still holds its document(s)
    QCOMPARE(rag.documents().size(), 1);
    QVERIFY(rag.documentCount() > 0);
}

void TestRag::toolSearchResult() {
    KnowledgeSearchTool tool(&rag);
    const ToolResult r = tool.execute(QJsonObject{{"query", "alpha"}});
    QVERIFY(r.success);
    QVERIFY(r.output.contains("doc2.txt"));
    QVERIFY(r.output.contains("score"));

    const ToolResult bad = tool.execute(QJsonObject{}); // no query
    QVERIFY(!bad.success);
}

QTEST_GUILESS_MAIN(TestRag)
#include "test_rag.moc"
