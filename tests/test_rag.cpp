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
    void chineseDocumentChunked();
    void batchEmbedFailureFallsBackToPseudo();
    void batchEmbedSuccessKeepsRealVectors();
    void oversizeDocumentRejected();
    void documentChunksListing();

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

// Chinese text has no spaces: the chunker must still split it into several
// passages (the old whitespace splitter returned a single giant chunk) and
// search must find a phrase from it.
void TestRag::chineseDocumentChunked() {
    QString text;
    const QString sentence = QStringLiteral(
        " NexusAI 是一个本地优先的智能体应用，支持检索增强生成。");
    for (int i = 0; i < 60; ++i)  // ~1900 chars, no spaces at all
        text += QStringLiteral("第%1章。").arg(i) + sentence;

    RagService local;
    const int chunks = local.ingestDocument("中文文档.txt", text);
    QVERIFY(chunks >= 2);

    const QJsonArray docs = local.documents();
    QCOMPARE(docs.size(), 1);
    QCOMPARE(docs.at(0).toObject().value("pseudo").toBool(), true); // offline mode

    const QJsonArray hits = local.search(QStringLiteral("检索增强生成"), 3);
    QVERIFY(!hits.isEmpty());
    bool foundPhrase = false;
    for (const auto& h : hits) {
        if (h.toObject().value("text").toString().contains(QStringLiteral("检索增强生成")))
            foundPhrase = true;
    }
    QVERIFY(foundPhrase);
}

// A failed batch embedding (e.g. Ollama offline) must degrade the WHOLE
// document to the pseudo embedding, never mix vector dimensions.
void TestRag::batchEmbedFailureFallsBackToPseudo() {
    RagService local;
    local.setBatchEmbedder([](const QStringList&) {
        return QVector<QVector<float>>{};   // network failure
    });
    const int chunks = local.ingestDocument("doc3.txt", longDoc("three", 6));
    QVERIFY(chunks >= 2);

    const QJsonArray docs = local.documents();
    QCOMPARE(docs.at(0).toObject().value("pseudo").toBool(), true);

    const QJsonArray hits = local.search("alpha", 3);
    QVERIFY(!hits.isEmpty()); // still searchable via pseudo embedding
}

// Successful batch embeddings keep the real vectors; a query whose embedder
// returns a mismatched dimension must fall back instead of scoring 0.
void TestRag::batchEmbedSuccessKeepsRealVectors() {
    RagService local;
    local.setEmbedder([](const QString& text) {
        // 8-dim "real" embedding driven by simple text features
        QVector<float> v(8, 0.0f);
        for (QChar c : text) v[c.unicode() % 8] += 1.0f;
        return v;
    });
    local.setBatchEmbedder([](const QStringList& texts) {
        QVector<QVector<float>> out;
        for (const QString& t : texts) {
            QVector<float> v(8, 0.0f);
            for (QChar c : t) v[c.unicode() % 8] += 1.0f;
            out.append(v);
        }
        return out;
    });

    QVERIFY(local.ingestDocument("doc4.txt", longDoc("four", 6)) >= 2);
    QCOMPARE(local.documents().at(0).toObject().value("pseudo").toBool(), false);
    QVERIFY(!local.search("alpha", 3).isEmpty());

    // Dimension drift (embedder replaced/changed model): query falls back
    // to the pseudo embedding and still returns scored chunks.
    local.setEmbedder([](const QString&) {
        return QVector<float>(3, 1.0f);   // wrong dimension on purpose
    });
    QVERIFY(!local.search("alpha", 3).isEmpty());
}

void TestRag::oversizeDocumentRejected() {
    RagService local;
    // Enough chars to exceed MaxChunksPerDocument chunks
    const QString huge = longDoc("big", (RagService::MaxChunksPerDocument + 2) * 10);
    QCOMPARE(local.ingestDocument("huge.txt", huge), -1);
    QVERIFY(local.isEmpty());
}

// documentChunks returns the stored chunks in order for the detail view
void TestRag::documentChunksListing() {
    RagService local;
    QVERIFY(local.ingestDocument("doc5.txt", longDoc("five", 6)) >= 2);

    const QJsonArray chunks = local.documentChunks("doc5.txt");
    QVERIFY(chunks.size() >= 2);
    for (int i = 0; i < chunks.size(); ++i) {
        const QJsonObject chunk = chunks.at(i).toObject();
        QCOMPARE(chunk.value("chunk_index").toInt(), i);   // ordered
        QVERIFY(chunk.value("chunk_total").toInt() == chunks.size());
        QVERIFY(!chunk.value("text").toString().isEmpty());
    }
    QVERIFY(local.documentChunks("missing.txt").isEmpty());
}

QTEST_GUILESS_MAIN(TestRag)
#include "test_rag.moc"
