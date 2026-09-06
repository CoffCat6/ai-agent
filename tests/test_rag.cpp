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
    // Batch 6: persistence + calibration
    void restoreFromPersistedChunksNeedsNoEmbedding();
    void queryEmbeddingIsCached();
    void retrievalCalibrationLabeledSet();
    // Regression guard for the Batch 6 hotfix: the UI knowledge list is
    // driven by changed(); ingestion lost that emission once already.
    void ingestEmitsChanged();
    void documentPersistedDeliversAcrossThreads();
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


// ── Batch 6: persistence + calibration ────────────────────

// A document rebuilt from persisted chunks must be searchable with ZERO
// embedding calls - that is the whole point of the chunk store. The embed
// call counter must stay at 0 across restore AND search.
void TestRag::restoreFromPersistedChunksNeedsNoEmbedding() {
    int embedCalls = 0;
    auto embed = [&embedCalls](const QString& text) {
        ++embedCalls;
        QVector<float> v(8, 0.0f);
        for (QChar c : text) v[c.unicode() % 8] += 1.0f;
        return v;
    };

    RagService source;
    source.setEmbedder(embed);
    source.setBatchEmbedder([embed](const QStringList& texts) {
        QVector<QVector<float>> out;
        for (const QString& t : texts) out.append(embed(t));
        return out;
    });
    source.setEmbeddingModelName(QStringLiteral("m1"));
    QVERIFY(source.ingestDocument("doc.txt", longDoc("persist", 6)) >= 2);

    // The chunks round-trip through a float blob exactly (bit-consistent).
    const QVector<KnowledgeChunk> chunks = source.exportPersistedChunks("doc.txt");
    QVERIFY(chunks.size() >= 2);
    QVERIFY(!chunks.first().pseudo);

    RagService restored;   // fresh service, embedder ONLINE but must be unused
    restored.setEmbedder(embed);
    restored.setEmbeddingModelName(QStringLiteral("m1"));
    const int callsBefore = embedCalls;
    QVERIFY(restored.restorePersistedDocument(
        "doc.txt", chunks, restored.restoreGeneration()));
    QCOMPARE(embedCalls, callsBefore);        // restore: zero embedding IO

    // Querying the restored REAL-mode store needs exactly one query
    // embedding (restore itself was free; the query vector is then cached).
    const QJsonArray hits = restored.search("persist alpha", 3);
    QCOMPARE(embedCalls, callsBefore + 1);
    QVERIFY(!hits.isEmpty());
    QCOMPARE(hits.first().toObject().value("source").toString(), QStringLiteral("doc.txt"));
    QVERIFY(!hits.first().toObject().value("text").toString().isEmpty());
    restored.search("persist alpha", 3);      // repeat → served from cache
    QCOMPARE(embedCalls, callsBefore + 1);
}

// KB-605: identical query texts reuse the cached embedding - one network
// round-trip per unique query, not one per search call.
void TestRag::queryEmbeddingIsCached() {
    int embedCalls = 0;
    RagService local;
    local.setEmbedder([&embedCalls](const QString& text) {
        ++embedCalls;
        QVector<float> v(8, 0.0f);
        for (QChar c : text) v[c.unicode() % 8] += 1.0f;
        return v;
    });
    local.setBatchEmbedder([](const QStringList& texts) {
        QVector<QVector<float>> out;
        for (int i = 0; i < texts.size(); ++i)
            out.append(QVector<float>(8, 0.5f));
        return out;
    });
    QVERIFY(local.ingestDocument("doc.txt", longDoc("cache", 6)) >= 2);

    local.search("alpha", 3);
    const int afterFirst = embedCalls;
    QVERIFY(afterFirst > 0);
    local.search("alpha", 3);   // cache hit
    local.search("alpha", 3);   // cache hit
    QCOMPARE(embedCalls, afterFirst);
    local.search("beta", 3);    // different text → new embedding
    QVERIFY(embedCalls > afterFirst);
}

// KB-604: calibration harness over a labeled set (deterministic keyword-bag
// embedder standing in for the real model). Relevant queries must hit their
// topic's document above the relevance threshold; unrelated queries must
// degrade to no-match. Guards the 0.15 placeholder against regressions.
void TestRag::retrievalCalibrationLabeledSet() {
    const QStringList topics = {
        "apple banana fruit vitamin",          // topic 0
        "quantum particle physics energy",     // topic 1
        "decorator python function syntax",    // topic 2
        "database index query latency",        // topic 3
    };
    auto embed = [&topics](const QString& text) {
        QVector<float> v(topics.size(), 0.0f);
        for (int t = 0; t < topics.size(); ++t) {
            const QStringList words = topics.at(t).split(u' ');
            for (const QString& w : words) {
                if (text.contains(w, Qt::CaseInsensitive)) v[t] += 1.0f;
            }
        }
        return v;
    };

    RagService local;
    local.setEmbedder(embed);
    local.setBatchEmbedder([embed](const QStringList& texts) {
        QVector<QVector<float>> out;
        for (const QString& t : texts) out.append(embed(t));
        return out;
    });

    // two documents per topic, CJK+latin mixed content
    QVERIFY(local.ingestDocument("apple-a.txt",
        QStringLiteral("The apple banana fruit vitamin content explains "
                       "nutrition and dietary fiber in detail. ")) >= 1);
    const auto topicDoc = [](const QString& sentence) {
        QString text;
        for (int i = 0; i < 12; ++i) text += sentence;   // safely > 512 chars → 2+ chunks
        return text;
    };
    QVERIFY(local.ingestDocument("apple-b.txt",
        topicDoc(QStringLiteral("apple banana fruit vitamin studies "
                                 "repeated in dietary research. "))) >= 2);
    QVERIFY(local.ingestDocument("quantum-a.txt",
        topicDoc(QStringLiteral("quantum particle physics energy levels "
                                "explained by entanglement models. "))) >= 2);
    QVERIFY(local.ingestDocument("python-a.txt",
        topicDoc(QStringLiteral("decorator python function syntax wraps "
                                "callables with examples. "))) >= 2);
    QVERIFY(local.ingestDocument("database-a.txt",
        topicDoc(QStringLiteral("database index query latency tuning guide "
                                "with benchmarks. "))) >= 2);

    struct Labeled { const char* query; const char* topic; }; // "" = no-match
    const QVector<Labeled> labeled = {
        {"apple fruit vitamin", "apple"},
        {"banana fruit", "apple"},
        {"quantum physics energy", "quantum"},
        {"quantum particle", "quantum"},
        {"python decorator function", "python"},
        {"decorator syntax", "python"},
        {"database index latency", "database"},
        {"query latency", "database"},
        {"weather forecast tomorrow", ""},          // unrelated → no-match
        {"recipe pasta tomato", ""},                // unrelated → no-match
    };

    int relevantTop1 = 0, relevantTotal = 0, noMatchTotal = 0;
    for (const Labeled& item : labeled) {
        const QJsonArray hits = local.search(QString::fromLatin1(item.query), 2);
        if (qstrlen(item.topic) == 0) {
            ++noMatchTotal;
            QVERIFY2(hits.isEmpty(),
                     qPrintable(QStringLiteral("unrelated query '%1' returned results")
                                    .arg(item.query)));
            continue;
        }
        ++relevantTotal;
        QVERIFY2(!hits.isEmpty(),
                 qPrintable(QStringLiteral("relevant query '%1' returned nothing")
                                .arg(item.query)));
        const QString source = hits.first().toObject().value("source").toString();
        if (source.startsWith(QString::fromLatin1(item.topic))) ++relevantTop1;
    }
    // Calibration bar: every relevant query lands in its topic, every
    // unrelated query degrades to no-match.
    QCOMPARE(relevantTop1, relevantTotal);
    QVERIFY(relevantTotal >= 8);
    QCOMPARE(noMatchTotal, 2);
}


// The ContextPanel knowledge list refreshes on RagService::changed().
// Ingestion MUST keep emitting it (lost during the Batch 6 refactor, which
// blanked the UI list on every import/restore).
void TestRag::ingestEmitsChanged() {
    RagService local;
    QSignalSpy spy(&local, &RagService::changed);
    QVERIFY(local.ingestDocument("doc.txt", longDoc("one", 6)) >= 2);
    QVERIFY2(spy.count() >= 1, "ingestDocument no longer emits changed()");
    QVERIFY(local.ingestDocument("doc.txt", longDoc("two", 6)) >= 2);
    QVERIFY2(spy.count() >= 2, "re-ingest no longer emits changed()");
}

// documentPersisted crosses a thread boundary (ingest runs on the
// QtConcurrent pool, persistence happens on the GUI thread) - the queued
// delivery must survive with its custom QVector<KnowledgeChunk> payload.
void TestRag::documentPersistedDeliversAcrossThreads() {
    RagService local;
    local.setBatchEmbedder([](const QStringList& texts) {
        QVector<QVector<float>> out;
        for (int i = 0; i < texts.size(); ++i)
            out.append(QVector<float>(8, 0.5f));
        return out;
    });

    QAtomicInt delivered{0};
    QString deliveredName;
    QString deliveredContent;
    QVector<KnowledgeChunk> deliveredChunks;
    QObject::connect(&local, &RagService::documentPersisted, &local,
        [&](const QString& name, const QString& content,
            const QVector<KnowledgeChunk>& chunks) {
            delivered.fetchAndAddRelaxed(1);
            deliveredName = name;
            deliveredChunks = chunks;
            deliveredContent = content;
        }, Qt::QueuedConnection);

    const int chunks = local.ingestDocument("doc.txt", longDoc("queue", 6));
    QVERIFY(chunks >= 2);

    QTRY_COMPARE_WITH_TIMEOUT(delivered.loadRelaxed(), 1, 5000);
    QCOMPARE(deliveredName, QStringLiteral("doc.txt"));
    QVERIFY(!deliveredContent.isEmpty());
    QCOMPARE(deliveredChunks.size(), chunks);
    QVERIFY(!deliveredChunks.first().embedding.isEmpty());
}

QTEST_GUILESS_MAIN(TestRag)
#include "test_rag.moc"
