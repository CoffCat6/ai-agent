// RagService: multi-document ingest/search/remove with the offline
// pseudo-embedding fallback, and the KnowledgeSearchTool surface.
//
// Every test owns its RagService instance (FIX-304): the tests previously
// shared one fixture service, which made removeDocument/toolOnEmptyBase
// depend on ingestions performed by earlier tests in declaration order.
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
    void knowledgeToolDescriptionIsConditional();
    void chineseDocumentChunked();
    void batchEmbedFailureFallsBackToPseudo();
    void batchEmbedSuccessKeepsRealVectors();
    // Batch 3: retrieval correctness (FIX-301/302) and restore races (FIX-303)
    void unrelatedOrWeakQueryReturnsNoMatch();
    void dimensionMismatchQueryReturnsNoMatch();
    void restoreGenerationCancelsStaleIngest();
    void oversizeDocumentRejected();
    void documentChunksListing();
};

void TestRag::ingestAndListDocuments() {
    RagService rag;
    const int chunks = rag.ingestDocument("doc1.txt", longDoc("one", 10));
    QVERIFY(chunks >= 2); // chunked into several passages

    const QJsonArray docs = rag.documents();
    QCOMPARE(docs.size(), 1);
    QCOMPARE(docs.at(0).toObject().value("name").toString(), QStringLiteral("doc1.txt"));
    QVERIFY(docs.at(0).toObject().value("chunks").toInt() >= 2);
}

void TestRag::searchReturnsScoredChunks() {
    RagService rag;
    rag.ingestDocument("doc2.txt", longDoc("two", 8));
    const QJsonArray hits = rag.search("alpha beta", 4);
    QVERIFY(!hits.isEmpty());
    QVERIFY(hits.size() <= 4);
    for (const auto& entry : hits) {
        const QJsonObject hit = entry.toObject();
        QVERIFY(hit.contains("score"));
        QCOMPARE(hit.value("source").toString(), QStringLiteral("doc2.txt"));
        QVERIFY(!hit.value("text").toString().isEmpty());
    }
}

void TestRag::removeDocument() {
    RagService rag;
    rag.ingestDocument("doc1.txt", longDoc("one", 6));
    rag.ingestDocument("doc2.txt", longDoc("two", 6));

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
    RagService rag;
    rag.ingestDocument("doc2.txt", longDoc("two", 8));
    QVERIFY(rag.documentCount() > 0);
    rag.ingestDocument("doc2.txt", longDoc("two-again", 8));
    const QJsonArray docs = rag.documents();
    QCOMPARE(docs.size(), 1); // replaced, not duplicated
}

void TestRag::emptyInputRejected() {
    RagService rag;
    QCOMPARE(rag.ingestDocument("empty.txt", "   "), 0);
    QCOMPARE(rag.ingestDocument("", "content"), 0);
}

void TestRag::toolOnEmptyBase() {
    RagService emptyRag;
    KnowledgeSearchTool tool(&emptyRag);
    const ToolResult r = tool.execute(QJsonObject{{"query", "anything"}});
    QVERIFY(r.success);
    QVERIFY(r.output.contains("empty"));
}

void TestRag::toolSearchResult() {
    RagService rag;
    rag.ingestDocument("doc2.txt", longDoc("two", 8));

    KnowledgeSearchTool tool(&rag);
    const ToolResult r = tool.execute(QJsonObject{{"query", "alpha"}});
    QVERIFY(r.success);
    QVERIFY(r.output.contains("doc2.txt"));
    QVERIFY(r.output.contains("score"));
    // SEC-502: passages are wrapped as untrusted DATA, never bare text -
    // a poisoned document must not read like model instructions.
    QVERIFY(r.output.contains("<knowledge_passage"));
    QVERIFY(r.output.contains("</knowledge_passage>"));
    QVERIFY(r.output.contains("untrusted"));

    const ToolResult bad = tool.execute(QJsonObject{}); // no query
    QVERIFY(!bad.success);
}

// FIX-503: the description must invite conditional use, not mandate the
// tool - "FIRST and REQUIRED" biased weak models into calling the KB on
// every message.
void TestRag::knowledgeToolDescriptionIsConditional() {
    KnowledgeSearchTool tool(nullptr);
    const ToolDefinition def = tool.definition();
    QVERIFY2(!def.description.contains("FIRST"),
             "description must not demand being called first");
    QVERIFY2(!def.description.contains("REQUIRED"),
             "description must not mark the call as required");
    QVERIFY(def.description.contains("ONLY"));
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

// Successful batch embeddings keep the real vectors. A query whose embedder
// returns a MISMATCHED dimension must degrade to no-match: scoring a
// real-mode store with a pseudo/fallback vector yields a similarity of 0
// for every chunk, so the returned "top-k" would be arbitrary noise.
// (Audit P1-4 / Batch 3 FIX-302 - this flips the previously codified
// fallback behavior.)
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

    // Dimension drift (embedder replaced/changed model): no-match, no noise.
    local.setEmbedder([](const QString&) {
        return QVector<float>(3, 1.0f);   // wrong dimension on purpose
    });
    QVERIFY(local.search("alpha", 3).isEmpty());
}

// FIX-301: the relevance gate. A real-mode store must not return passages
// whose similarity is orthogonal (score 0) or below the relevance threshold
// - previously search() returned an unconditional top-k, so "No passages
// matched" was unreachable for non-empty knowledge bases.
void TestRag::unrelatedOrWeakQueryReturnsNoMatch() {
    RagService local;
    auto embed = [](const QString& text) {
        QVector<float> v(8, 0.0f);
        if (text.contains(QLatin1String("apple"))) v[0] += 1.0f;
        if (text.contains(QLatin1String("quantum"))) v[1] += 1.0f;
        if (text.contains(QLatin1String("weak"))) { v[0] += 0.1f; v[1] += 1.0f; }
        return v;
    };
    local.setEmbedder(embed);
    local.setBatchEmbedder([embed](const QStringList& texts) {
        QVector<QVector<float>> out;
        for (const QString& t : texts) out.append(embed(t));
        return out;
    });

    QVERIFY(local.ingestDocument("apples.txt", longDoc("apple", 6)) >= 2);
    QCOMPARE(local.documents().at(0).toObject().value("pseudo").toBool(), false);

    QVERIFY(!local.search("apple", 3).isEmpty());   // strong match survives
    QVERIFY(local.search("quantum", 3).isEmpty());  // orthogonal → no-match
    QVERIFY(local.search("weak", 3).isEmpty());     // ~0.1 < threshold → no-match
}

// FIX-302: a real-mode store whose query vector is missing (embedder
// offline) or dimension-drifted must return no-match instead of silently
// switching to the pseudo vector (whose dimension cannot match - every
// similarity would be 0 and the "top-k" would be arbitrary chunks).
void TestRag::dimensionMismatchQueryReturnsNoMatch() {
    RagService local;
    local.setBatchEmbedder([](const QStringList& texts) {
        QVector<QVector<float>> out;
        for (int i = 0; i < texts.size(); ++i)
            out.append(QVector<float>(8, 0.5f));
        return out;
    });
    local.setEmbedder([](const QString&) {
        return QVector<float>{};   // embedder offline at query time
    });

    QVERIFY(local.ingestDocument("doc6.txt", longDoc("six", 6)) >= 2);
    QVERIFY(local.search("anything", 3).isEmpty());   // offline → no-match

    local.setEmbedder([](const QString&) {
        return QVector<float>(3, 1.0f);   // dimension drift
    });
    QVERIFY(local.search("anything", 3).isEmpty());   // drift → no-match
}

// FIX-303: a background restore must stop re-adding documents once the user
// cleared or removed knowledge during the restore. clear()/removeDocument()
// bump a restore generation; restore ingestion binds to the generation it
// started with and becomes a no-op when superseded.
void TestRag::restoreGenerationCancelsStaleIngest() {
    RagService local;
    const quint64 generation = local.restoreGeneration();

    QCOMPARE(local.ingestRestoredDocument("a.txt", longDoc("a", 6), generation), 2);
    QVERIFY(!local.isEmpty());

    local.clear();   // user wiped the base mid-restore → generation bumped

    QCOMPARE(local.ingestRestoredDocument("b.txt", longDoc("b", 6), generation), -2);
    QVERIFY(local.isEmpty());   // stale restore step must not re-add content

    // A fresh restore (new generation) works again after the clear.
    QCOMPARE(local.ingestRestoredDocument("c.txt", longDoc("c", 6),
                                          local.restoreGeneration()), 2);
    QVERIFY(!local.isEmpty());
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
