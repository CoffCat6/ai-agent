#pragma once

#include "../tools/ITool.h"
#include "RagService.h"

namespace NexusAI {

// Safe (auto-executed) tool that lets the model query the user's imported
// knowledge base. Registered in main.cpp with the application RagService.
class KnowledgeSearchTool : public ITool {
public:
    explicit KnowledgeSearchTool(RagService* rag) : m_rag(rag) {}

    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = "search_knowledge_base";
        def.description =
            "Search the user's local knowledge base (documents imported in the app's "
            "knowledge panel) for passages relevant to a query. This is the FIRST and "
            "REQUIRED tool for any question about imported/knowledge-base/document "
            "content - the base is stored inside the app, so do NOT use file or shell "
            "tools to look for it on disk. Returns scored text chunks with their "
            "source document.";
        def.level = ToolLevel::Safe;
        QJsonObject props;
        props["query"] = stringParam("The search query.");
        QJsonObject kProp;
        kProp["type"] = "integer";
        kProp["description"] = "Maximum number of passages to return (1-10, default 4).";
        props["k"] = kProp;
        def.parameters = buildParams(props, QJsonArray{"query"});
        return def;
    }

    ToolResult execute(const QJsonObject& args) override {
        if (!m_rag)
            return ToolResult::fail("Knowledge base is unavailable");

        const QString query = args.value("query").toString().trimmed();
        if (query.isEmpty())
            return ToolResult::fail("Query is required");

        if (m_rag->isEmpty())
            return ToolResult::ok("The knowledge base is empty. "
                "Tell the user they can import documents via the knowledge "
                "base panel and try again later.");

        int k = args.value("k").toInt(4);
        k = qBound(1, k, 10);

        const QJsonArray results = m_rag->search(query, k);
        if (results.isEmpty())
            return ToolResult::ok(QStringLiteral("No passages matched '%1'.").arg(query));

        QStringList lines;
        for (const auto& entry : results) {
            const QJsonObject r = entry.toObject();
            lines << QStringLiteral("[score %1 | %2, chunk %3/%4]\n%5")
                .arg(r.value("score").toDouble(), 0, 'f', 3)
                .arg(r.value("source").toString())
                .arg(r.value("chunk_index").toInt() + 1)
                .arg(r.value("chunk_total").toInt())
                .arg(r.value("text").toString());
        }
        return ToolResult::ok(lines.join(QStringLiteral("\n---\n")));
    }

private:
    static QJsonObject stringParam(const QString& description) {
        QJsonObject p;
        p["type"] = "string";
        p["description"] = description;
        return p;
    }
    static QJsonObject buildParams(const QJsonObject& properties, const QJsonArray& required) {
        QJsonObject params;
        params["type"] = "object";
        params["properties"] = properties;
        params["required"] = required;
        return params;
    }

    RagService* m_rag;
};

} // namespace NexusAI
