// RagService: multi-document ingest/search/remove with the offline
// pseudo-embedding fallback, and the KnowledgeSearchTool surface.
//
// Every test owns its RagService instance (FIX-304): the tests previously
// shared one fixture service, which made removeDocument/toolOnEmptyBase
// depend on ingestions performed by earlier tests in declaration order.
#include "core/rag/RagService.h"
#include "core/rag/DocumentRetriever.h"
#include "core/rag/KnowledgeSearchTool.h"
#include "viewmodels/PdfTextExtractor.h"

#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QFuture>
#include <QtConcurrent/QtConcurrentRun>
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
    void toolReportsOfflineEmbedder();
    void toolSearchResult();
    void knowledgeToolDescriptionIsConditional();
    void toolResultCarriesCitationMarkers();
    void citationMarkerWithBracketInName();
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
    // docs/23 G6: the chunk budget gates the restore path too.
    void restoreRespectsTotalChunkBudget();
    // docs/25: PDF 页码映射
    void pageMappingWritesChunkPage();
    void pageMappingAbsentForPlainText();
    void queryEmbeddingIsCached();
    void retrievalCalibrationLabeledSet();
    // Batch 9: RAG matrix (TEST-904)
    void documentUpdateAndRemoveSearch();
    void multiDocumentMergeHits();
    // Batch 10: total-chunk governance (REL-1003)
    void totalChunkCapGovernsIngest();
    // Regression guard for the Batch 6 hotfix: the UI knowledge list is
    // driven by changed(); ingestion lost that emission once already.
    void ingestEmitsChanged();
    void documentPersistedDeliversAcrossThreads();
    // docs/20 §5.2 (C2): PDF import — text extraction step
    void extractPdfTextFromSamplePages();
    void extractPdfTextRejectsNonPdfFile();
    void extractPdfTextMissingFile();
    void extractPdfTextFromWorkerThread();
    // docs/25 B5: finishSearch snapshots retrievers (shared_ptr) under the
    // lock — a remove after a completed search must not corrupt anything,
    // and documents that were NOT removed keep searching normally.
    void searchSnapshotSurvivesRemove();
    // docs/25 B6: search() ranks with std::partial_sort — topK is a hard cap
    // and the survivors come back in descending score order.
    void searchCapsAtTopKInScoreOrder();
    // docs/25 B7: chunk-once ingest — addChunkedDocument behaves exactly
    // like addDocument's internal chunking of the same trimmed text.
    void chunkedDocumentIngestMatchesAddDocument();
    // docs/25 B7: an oversize PDF (pageTexts set) is rejected by the budget
    // pre-check BEFORE any chunking/embedding work.
    void oversizePdfRejectedByPrecheck();
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

// 嵌入服务离线（Ollama 没启动）时，工具必须**失败并说明原因**，而不是回
// "No passages matched"——后者会被模型和用户理解成"文档里没有"，于是
// "服务没启动"看起来像"知识库白导了"。这正是"文档已导入但模型找不到"的成因。
void TestRag::toolReportsOfflineEmbedder() {
    RagService local;
    bool online = true;
    const auto vec = [&online]() {
        return online ? QVector<float>(8, 0.5f) : QVector<float>{};
    };
    local.setBatchEmbedder([&vec](const QStringList& texts) {
        QVector<QVector<float>> out;
        for (int i = 0; i < texts.size(); ++i) out.append(vec());
        return out;
    });
    local.setEmbedder([&vec](const QString&) { return vec(); });
    QVERIFY(local.ingestDocument("doc9.txt", longDoc("offline", 6)) >= 2);

    KnowledgeSearchTool tool(&local);
    QVERIFY(tool.execute(QJsonObject{{"query", "offline"}}).success);   // 在线：正常

    online = false;   // Ollama 掉线
    // 换一个**未被缓存**的 query：已算好的向量依然有效（服务掉线不影响缓存），
    // 只有新查询才真正走不到嵌入。
    const ToolResult down = tool.execute(QJsonObject{{"query", "offline second probe"}});
    QVERIFY2(!down.success,
             "offline embedder must FAIL, not silently report no-match");
    QVERIFY2(down.error.contains(QStringLiteral("embedding service")),
             qPrintable(down.error));
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

// CITE-801 (docs/20 §5.1): every hit is prefixed with a machine-readable
// provenance marker [doc:name#chunkId] so the model can cite sources and the
// UI can turn "[文档名]" into a clickable chip.
void TestRag::toolResultCarriesCitationMarkers() {
    // Hostile-ish name: quotes/angle brackets are SEC-502-escaped in the
    // <knowledge_passage source="..."> attribute, but the marker must keep
    // the RAW name — the model copies it verbatim into [文档名] and the UI
    // resolves it against the raw document list.
    const QString rawName = QStringLiteral("cite q\"1<x>.txt");

    RagService empty;
    KnowledgeSearchTool emptyTool(&empty);
    QVERIFY(!emptyTool.knowledgeBaseNonEmpty());   // empty base: no prompt injection

    RagService rag;
    QVERIFY(rag.ingestDocument(rawName, longDoc("cite", 8)) >= 2);
    KnowledgeSearchTool tool(&rag);
    QVERIFY(tool.knowledgeBaseNonEmpty());

    const ToolResult r = tool.execute(QJsonObject{{"query", "alpha"}});
    QVERIFY2(r.success, qPrintable(r.error));

    const QStringList blocks = r.output.split(QStringLiteral("\n---\n"));
    QVERIFY2(blocks.size() >= 3,
             qPrintable(QStringLiteral("expected header + several hits, got %1 blocks")
                            .arg(blocks.size())));
    // blocks[0] is the SEC-502 framing header, entries follow.
    QVERIFY2(blocks.first().contains(QStringLiteral("untrusted")),
             qPrintable(blocks.first()));

    static const QRegularExpression markerRe(
        QStringLiteral("^\\[doc:(.*)#(\\d+)\\]"));
    int rawMarkerCount = 0;
    for (int b = 1; b < blocks.size(); ++b) {
        const QString& block = blocks.at(b);
        // Marker first, then the score/chunk line, then the passage wrapper.
        const int markerEnd = block.indexOf(QLatin1Char(']'));
        QVERIFY2(markerEnd > 0,
                 qPrintable(QStringLiteral("block lacks [doc:...#...] marker:\n%1").arg(block)));
        const QRegularExpressionMatch m = markerRe.match(block);
        QVERIFY2(m.hasMatch(), qPrintable(block));
        if (m.captured(1) == rawName) ++rawMarkerCount;

        // chunkId is 0-based chunk_index; the human line shows 1-based N.
        const QString chunkField =
            QStringLiteral(", chunk %1/").arg(m.captured(2).toInt() + 1);
        QVERIFY2(block.contains(chunkField),
                 qPrintable(QStringLiteral("chunkId %1 not consistent with '%2' in:\n%3")
                                .arg(m.captured(2), chunkField, block)));

        QVERIFY2(block.contains(QStringLiteral("<knowledge_passage")),
                 qPrintable(block));
    }
    // Raw-name fidelity: markers carried the unescaped name, while the
    // source attribute stayed SEC-502-escaped.
    QCOMPARE(rawMarkerCount, blocks.size() - 1);   // header block excluded
    QVERIFY2(!r.output.contains(QStringLiteral("[doc:cite q'1(x).txt#")),
             "marker must use the raw name, not the SEC-502-escaped one");
    QVERIFY2(r.output.contains(QStringLiteral("source=\"cite q'1(x).txt\"")),
             qPrintable(r.output));

    // Every <knowledge_passage> sits behind a marker (split parity already
    // proves one marker per block; this guards the ordering).
    int passages = r.output.count(QStringLiteral("<knowledge_passage"));
    int markers = 0;
    for (int pos = 0; (pos = r.output.indexOf(QStringLiteral("[doc:"), pos)) >= 0; pos += 5)
        ++markers;
    QCOMPARE(markers, passages);
    QCOMPARE(passages, blocks.size() - 1);   // header block excluded
}

// CITE-801 边界：文档名含 ']' 时记号会提前闭合，模型照抄出的名字与真实
// 文档名对不上。这是 [文档名] 记号语法本身的固有限制（不是转义能修的——
// 转义会破坏"原始名往返"契约）。锁住实际行为：标记仍然携带完整原始名
// （模型能读到），而不会错认成另一个文档。
void TestRag::citationMarkerWithBracketInName() {
    const QString name = QStringLiteral("weird]name.txt");
    RagService rag;
    QVERIFY(rag.ingestDocument(name, longDoc("bracket", 6)) >= 2);
    KnowledgeSearchTool tool(&rag);
    const ToolResult r = tool.execute(QJsonObject{{"query", "alpha"}});
    QVERIFY2(r.success, qPrintable(r.error));

    // 原始名完整出现在输出里（模型可见），SEC-502 属性侧同样保留原名。
    QVERIFY2(r.output.contains(QStringLiteral("[doc:weird]name.txt#")),
             qPrintable(r.output));
    // 每个 chunk 都有自己的标记 + 一个人类可读的 chunk N/M 行。
    const QStringList blocks = r.output.split(QStringLiteral("\n---\n"));
    for (int b = 1; b < blocks.size(); ++b)
        QVERIFY2(blocks.at(b).contains(QStringLiteral("<knowledge_passage")),
                 qPrintable(blocks.at(b)));
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

    // searchDetailed 必须把"检索根本没跑起来"与"确实没有匹配"区分开：
    // 两者对 search() 都是空数组，但前者意味着嵌入服务（Ollama）没启动，
    // 工具层要把这个原因报给模型和用户，否则会被理解成"文档里没有"。
    {
        const RagService::SearchOutcome offline = local.searchDetailed("anything", 3);
        QVERIFY(offline.hasDocuments);
        QVERIFY(offline.embeddingUnavailable);
        QVERIFY(offline.passages.isEmpty());
    }

    local.setEmbedder([](const QString&) {
        return QVector<float>(3, 1.0f);   // dimension drift
    });
    QVERIFY(local.search("anything", 3).isEmpty());   // drift → no-match

    // 维度漂移同样是"查询向量对不上真实向量库"，归入同一降级状态。
    QVERIFY(local.searchDetailed("anything", 3).embeddingUnavailable);

    // 嵌入恢复可用后不再是降级状态（维度与入库时一致）。
    local.setEmbedder([](const QString&) {
        return QVector<float>(8, 0.5f);
    });
    QVERIFY(!local.searchDetailed("anything", 3).embeddingUnavailable);
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


// ── Batch 9: TEST-904 RAG update/remove/merge ─────────────
// Re-ingesting a name replaces its chunks: the old content must become
// unreachable (no-match), the new content retrievable. Removing a document
// makes its content unreachable too. Real-mode keyword embedder keeps the
// assertions deterministic.
void TestRag::documentUpdateAndRemoveSearch() {
    RagService local;
    auto embed = [](const QString& text) {
        QVector<float> v(8, 0.0f);
        if (text.contains(QLatin1String("penguin"))) v[0] += 1.0f;
        if (text.contains(QLatin1String("otter"))) v[1] += 1.0f;
        return v;
    };
    local.setEmbedder(embed);
    local.setBatchEmbedder([embed](const QStringList& texts) {
        QVector<QVector<float>> out;
        for (const QString& t : texts) out.append(embed(t));
        return out;
    });

    QVERIFY(local.ingestDocument("birds.txt",
        QStringLiteral("penguin colony study. ").repeated(30)) >= 2);
    QVERIFY(!local.search("penguin", 3).isEmpty());

    // Update: same name, disjoint content → old vocabulary fully gone
    QVERIFY(local.ingestDocument("birds.txt",
        QStringLiteral("otter habitat survey. ").repeated(30)) >= 2);
    const auto otterHits = local.search("otter", 3);
    QVERIFY(!otterHits.isEmpty());
    QCOMPARE(otterHits.first().toObject().value("source").toString(),
             QStringLiteral("birds.txt"));
    QVERIFY(local.search("penguin", 3).isEmpty());

    // Remove → no-match at all
    QVERIFY(local.removeDocument("birds.txt"));
    QVERIFY(local.search("otter", 3).isEmpty());
}

// Global top-k merge: one query fans out over every document and the merged
// result carries hits from BOTH matching sources.
void TestRag::multiDocumentMergeHits() {
    RagService local;
    auto embed = [](const QString& text) {
        QVector<float> v(8, 0.0f);
        if (text.contains(QLatin1String("apple"))) v[0] += 1.0f;
        return v;
    };
    local.setEmbedder(embed);
    local.setBatchEmbedder([embed](const QStringList& texts) {
        QVector<QVector<float>> out;
        for (const QString& t : texts) out.append(embed(t));
        return out;
    });

    QVERIFY(local.ingestDocument("apple-a.txt",
        QStringLiteral("apple orchard report. ").repeated(30)) >= 2);
    QVERIFY(local.ingestDocument("apple-b.txt",
        QStringLiteral("apple harvest analysis. ").repeated(30)) >= 2);

    const auto hits = local.search("apple", 4);
    QVERIFY(hits.size() >= 2);
    bool sawA = false, sawB = false;
    for (const auto& h : hits) {
        const QString src = h.toObject().value("source").toString();
        if (src == QStringLiteral("apple-a.txt")) sawA = true;
        if (src == QStringLiteral("apple-b.txt")) sawB = true;
    }
    QVERIFY2(sawA && sawB, "multi-document merge lost a source");
}


// REL-1003: the knowledge base enforces a configurable total-chunk cap.
// Ingestion past the cap is rejected (-3) and frees up when documents are
// removed.
void TestRag::totalChunkCapGovernsIngest() {
    RagService local;
    local.setMaxTotalChunks(10);

    const int a = local.ingestDocument("a.txt", longDoc("a", 10));
    const int b = local.ingestDocument("b.txt", longDoc("b", 10));
    QVERIFY(a >= 2 && b >= 2);

    local.setMaxTotalChunks(a + b);   // exact fill → the next ingest rejects
    QCOMPARE(local.ingestDocument("c.txt", longDoc("c", 10)), -3);
    QVERIFY(!local.hasDocument("c.txt"));
    QCOMPARE(local.documentCount(), a + b);

    // Removing a document frees budget for new imports
    local.setMaxTotalChunks(a + b + 12);
    local.removeDocument("a.txt");
    QVERIFY(local.ingestDocument("c.txt", longDoc("c", 10)) >= 2);
}

// ── docs/20 §5.2 (workflow C2): PDF knowledge import ──────
// importDocument swaps ONLY the content-extraction step for .pdf files;
// these cases cover that step (dedup/naming/ingest stay where they were).

// Two-page sample (tests/resources/sample.pdf, generated by
// generate_sample_pdf.py): every page's text must come back, in order, and
// be directly ingestible by the unchanged RagService pipeline.
void TestRag::extractPdfTextFromSamplePages() {
    const QString path = QStringLiteral(NEXUS_TEST_RESOURCES_DIR "/sample.pdf");
    QVERIFY2(QFile::exists(path), qPrintable(path));

    const PdfExtractResult pdf = extractPdfText(path);
    QVERIFY2(pdf.ok(), qPrintable(pdf.detail));
    QVERIFY2(pdf.text.contains(QStringLiteral("NexusAI PDF import sample")),
             qPrintable(pdf.text));
    QVERIFY2(pdf.text.contains(QStringLiteral("page one")), qPrintable(pdf.text));
    QVERIFY2(pdf.text.contains(QStringLiteral("page two")), qPrintable(pdf.text));
    // Page 1 precedes page 2 (the per-page getAllText() loop keeps order).
    QVERIFY2(pdf.text.indexOf(QStringLiteral("page one"))
                 < pdf.text.indexOf(QStringLiteral("page two")),
             qPrintable(pdf.text));

    // End-to-end: the extracted text still flows through ingest unchanged.
    RagService rag;
    QVERIFY2(rag.ingestDocument("sample.pdf", pdf.text) >= 1, "extracted text unusable");
    QCOMPARE(rag.documents().size(), 1);
}

// A .pdf that is not a PDF must fail with a status, not crash and not
// silently produce garbage content for the knowledge base.
void TestRag::extractPdfTextRejectsNonPdfFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("garbage.pdf");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("this is definitely not a pdf document\n");
    file.close();

    const PdfExtractResult pdf = extractPdfText(path);
    QVERIFY(!pdf.ok());
    QVERIFY(pdf.text.isEmpty());
    QVERIFY2(!pdf.detail.isEmpty(), "failure must carry a reason");
    QVERIFY(pdf.status == PdfExtractStatus::Corrupted
            || pdf.status == PdfExtractStatus::NoText);
}

// Missing file → OpenFailed (AgentViewModel reports its own message on top).
void TestRag::extractPdfTextMissingFile() {
    const PdfExtractResult pdf =
        extractPdfText(QStringLiteral("no/such/file.pdf"));
    QVERIFY(!pdf.ok());
    QCOMPARE(pdf.status, PdfExtractStatus::OpenFailed);
    QVERIFY(pdf.text.isEmpty());
}

// AgentViewModel::importDocument 现在把 PDF 提取放到 QtConcurrent 的 worker
// 线程（docs/20 §5.2 修复：同步解析大 PDF 会冻结 GUI）。QPdfDocument 属于
// Qt6::Pdf（链接 Qt6::Gui），"文本提取是否真的能在非 GUI 线程跑"不能靠猜 ——
// 这里用真实 QThread 锁住该属性：若哪天换了个需要 GUI 线程的解析后端，
// 导入会在生产里直接崩或返回垃圾。
void TestRag::extractPdfTextFromWorkerThread() {
    const QString path = QStringLiteral(NEXUS_TEST_RESOURCES_DIR "/sample.pdf");
    QVERIFY2(QFile::exists(path), qPrintable(path));

    // 与生产路径完全一致：AgentViewModel::startPdfImport 用
    // QtConcurrent::run + QFutureWatcher<PdfExtractResult> 把解析放到 worker
    // 线程。QPdfDocument 属于 Qt6::Pdf（链接 Qt6::Gui），"文本提取是否真能在
    // 非 GUI 线程跑"不能靠猜 —— 这里锁住该属性：若哪天换了个需要 GUI 线程的
    // 解析后端，导入会在生产里崩或返回垃圾。
    QFuture<PdfExtractResult> future =
        QtConcurrent::run([path]() { return extractPdfText(path); });
    QVERIFY2(future.isStarted(), "QtConcurrent::run did not start");
    future.waitForFinished();
    QVERIFY2(future.isFinished(), "worker future never finished");

    const PdfExtractResult fromThread = future.result();
    QVERIFY2(fromThread.ok(), qPrintable(fromThread.detail));
    // 与主线程结果逐字节一致：证明解析不依赖线程局部状态。
    QCOMPARE(fromThread.text, extractPdfText(path).text);
    QCOMPARE(fromThread.status, PdfExtractStatus::Ok);
}

// docs/23 G6: the total-chunk budget also gates the restore path — an
// over-budget persisted document is skipped (it stays in SQLite and the
// startup loop reports the skip) instead of silently breaking the
// REL-1003 invariant on every launch.
void TestRag::restoreRespectsTotalChunkBudget() {
    int embedCalls = 0;
    auto embed = [&embedCalls](const QString& text) {
        ++embedCalls;
        QVector<float> v(8, 0.0f);
        for (QChar c : text) v[c.unicode() % 8] += 1.0f;
        return v;
    };

    RagService source;
    source.setEmbedder(embed);
    source.setBatchEmbedder([&embed](const QStringList& texts) {
        QVector<QVector<float>> out;
        for (const QString& t : texts) out.append(embed(t));
        return out;
    });
    source.setEmbeddingModelName(QStringLiteral("m1"));
    QVERIFY(source.ingestDocument("doc.txt", longDoc("persist", 4)) >= 2);
    const QVector<KnowledgeChunk> chunks = source.exportPersistedChunks("doc.txt");
    QVERIFY(chunks.size() >= 2);

    RagService restored;
    restored.setEmbedder(embed);
    restored.setEmbeddingModelName(QStringLiteral("m1"));
    // One chunk short of the requirement: restore must refuse.
    restored.setMaxTotalChunks(chunks.size() - 1);
    QVERIFY(!restored.restorePersistedDocument(
        "doc.txt", chunks, restored.restoreGeneration()));
    QVERIFY(restored.search(QStringLiteral("persist alpha"), 3).isEmpty());

    // Exact headroom restores fine (the same-name drop is accounted first).
    restored.setMaxTotalChunks(chunks.size());
    QVERIFY(restored.restorePersistedDocument(
        "doc.txt", chunks, restored.restoreGeneration()));
    QVERIFY(!restored.search(QStringLiteral("persist alpha"), 3).isEmpty());
}

    // docs/25: PDF 页码映射——ingestDocument 带 pageTexts 时，chunk 按
    // 规范化文本定位到页（1 基）；导出的持久化 chunk 携带 page；无页
    // 信息时 page 为 -1。
void TestRag::pageMappingWritesChunkPage() {
    int embedCalls = 0;
    auto embed = [&embedCalls](const QString&) {
        ++embedCalls;
        return QVector<float>(8, 0.25f);
    };

    RagService rag;
    rag.setEmbedder(embed);
    rag.setBatchEmbedder([&embed](const QStringList& texts) {
        QVector<QVector<float>> out;
        out.reserve(texts.size());
        for (int i = 0; i < texts.size(); ++i) out.append(QVector<float>(8, 0.25f));
        return out;
    });
    rag.setEmbeddingModelName(QStringLiteral("m1"));

    // 三页文档：每页 >512 字确保分块跨页；页内关键词差异化（避免
    // 重复段落命中错误页）。
    const auto makePage = [](const QString& keyword) {
        QString page;
        for (int i = 0; i < 40; ++i)
            page += QStringLiteral("第%1部分描述%2的详细情况，包含足够多的内容以触发分块机制。").arg(i).arg(keyword);
        return page;
    };
    const QStringList pageTexts = {
        makePage(QStringLiteral("苹果")),
        makePage(QStringLiteral("香蕉")),
        makePage(QStringLiteral("樱桃"))
    };
    const QString content = pageTexts.join(QChar(10));

    QCOMPARE(rag.ingestDocument("pdf-doc", content, pageTexts) > 0, true);
    const auto chunks = rag.documentChunks("pdf-doc");
    QVERIFY(chunks.size() >= 3);
    // 每个分块都能映射到页码，且首页 chunk 落在第 1 页。
    bool sawPage1 = false;
    for (const auto& chunkValue : chunks) {
        const int page = chunkValue.toObject().value("page").toInt(-1);
        QVERIFY2(page >= 1 && page <= 3,
                 qPrintable(QStringLiteral("unexpected page %1").arg(page)));
        if (page == 1) sawPage1 = true;
    }
    QVERIFY(sawPage1);
    QVERIFY(rag.usesPseudoEmbedding("pdf-doc") == false);
}

    // docs/25: 无页信息（文本文件导入）时 page 为 -1——UI 不显示页码。
void TestRag::pageMappingAbsentForPlainText() {
    RagService rag;
    rag.setEmbedder([](const QString&) { return QVector<float>(8, 0.25f); });
    rag.setBatchEmbedder([](const QStringList& texts) {
        QVector<QVector<float>> out;
        out.reserve(texts.size());
        for (int i = 0; i < texts.size(); ++i)
            out.append(QVector<float>(8, 0.25f));
        return out;
    });
    rag.setEmbeddingModelName(QStringLiteral("m1"));

    QVERIFY(rag.ingestDocument("plain.txt",
                               longDoc("plain", 6)) > 0);
    const auto chunks = rag.documentChunks("plain.txt");
    QVERIFY(!chunks.isEmpty());
    for (const auto& chunkValue : chunks)
        QCOMPARE(chunkValue.toObject().value("page").toInt(-1), -1);
}

// docs/25 B5: finishSearch copies a shared_ptr snapshot of the retrievers
// under the lock and does the cosine/rank work after releasing it. Removing
// a document right after a search must not corrupt the completed result,
// and documents that were NOT removed keep searching normally (the pre-B5
// raw-pointer copy would dangle once the removed retriever was destroyed).
void TestRag::searchSnapshotSurvivesRemove() {
    RagService rag;
    QVERIFY(rag.ingestDocument("a.txt", longDoc("alpha", 6)) > 0);
    QVERIFY(rag.ingestDocument("b.txt", longDoc("beta", 6)) > 0);

    QVERIFY(!rag.search("alpha beta", 4).isEmpty());

    rag.removeDocument("a.txt");
    // b.txt is untouched and still searchable through its own snapshot.
    const QJsonArray hits = rag.search("alpha beta", 4);
    QVERIFY(!hits.isEmpty());
    for (const auto& entry : hits)
        QCOMPARE(entry.toObject().value("source").toString(),
                 QStringLiteral("b.txt"));
    // Only a.txt left the store.
    QCOMPARE(rag.documents().size(), 1);
    QCOMPARE(rag.documents().at(0).toObject().value("name").toString(),
             QStringLiteral("b.txt"));
}

// docs/25 B6: search() uses std::partial_sort over the candidate chunks —
// topK is a hard cap and the survivors are in descending score order.
void TestRag::searchCapsAtTopKInScoreOrder() {
    RagService rag;
    QVERIFY(rag.ingestDocument("doc.txt", longDoc("one", 12)) >= 3);
    const QJsonArray top2 = rag.search("alpha beta", 2);
    QCOMPARE(top2.size(), 2);
    QVERIFY(top2.at(0).toObject().value("score").toDouble()
                >= top2.at(1).toObject().value("score").toDouble());
    // A wider topK never returns fewer hits than a narrower one.
    QVERIFY(rag.search("alpha beta", 8).size() >= top2.size());
}

// docs/25 B7: the PDF import path chunks once (for page mapping) and feeds
// the chunks straight to the retriever — addChunkedDocument must behave
// exactly like addDocument's internal chunking of the same trimmed text.
void TestRag::chunkedDocumentIngestMatchesAddDocument() {
    const QString doc = longDoc("gamma", 8).trimmed();
    const QStringList chunks = DocumentRetriever::chunkDocument(
        doc, DocumentRetriever::ChunkSize, DocumentRetriever::ChunkOverlap);
    QVERIFY(chunks.size() > 1);

    DocumentRetriever plain;
    DocumentRetriever prechunked;
    QVERIFY(plain.addDocument(doc, QJsonObject{{"source", "x"}}));
    QVERIFY(prechunked.addChunkedDocument(chunks, QJsonObject{{"source", "x"}}));
    QCOMPARE(prechunked.documentCount(), plain.documentCount());

    // Same search surface: identical hit count for the same query.
    // docs/25 E3：单参 search(query) 已删除，这里显式走向量版——两个检索器
    // 均为伪模式（无批量嵌入器），查询向量用本地 pseudoEmbed 计算，与原
    // 单参重载内部的行为完全一致。
    const QVector<float> queryPseudo =
        DocumentRetriever::pseudoEmbed(QStringLiteral("alpha beta"));
    QCOMPARE(prechunked.search({}, queryPseudo, 4).size(),
             plain.search({}, queryPseudo, 4).size());
}

// docs/25 B7: an oversize PDF (pageTexts set) is rejected by the budget
// pre-check BEFORE any chunking/embedding work — no wasted chunkDocument
// pass, no retriever, no document row.
void TestRag::oversizePdfRejectedByPrecheck() {
    RagService rag;
    const QString huge = longDoc("big", (RagService::MaxChunksPerDocument + 2) * 10);
    QCOMPARE(rag.ingestDocument("big.pdf", huge, {"page one", "page two"}), -1);
    QVERIFY(rag.isEmpty());
}

QTEST_GUILESS_MAIN(TestRag)
#include "test_rag.moc"
