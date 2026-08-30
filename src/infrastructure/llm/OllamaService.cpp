#include "OllamaService.h"

#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonArray>
#include <QByteArray>
#include <QDebug>

namespace NexusAI {

OllamaService::OllamaService(QObject* parent)
    : IAIService(parent), m_nam(new QNetworkAccessManager(this)) {}

OllamaService::~OllamaService() {
    if (m_currentReply) {
        m_currentReply->abort();
    }
}

QJsonObject OllamaService::buildRequestBody(const QJsonArray& messages, const QJsonArray& tools) {
    QJsonObject body;
    body["model"] = m_model;
    body["stream"] = true; // NDJSON streaming
    body["messages"] = convertMessagesForOllama(messages);

    // Ollama expects bare function schemas, not the OpenAI "type/function" wrapper
    if (!tools.isEmpty()) {
        QJsonArray ollamaTools;
        for (const auto& tool : tools) {
            const QJsonObject toolObj = tool.toObject();
            ollamaTools.append(toolObj.contains("function")
                ? toolObj.value("function") : toolObj);
        }
        body["tools"] = ollamaTools;
    }

    return body;
}

QJsonArray OllamaService::convertMessagesForOllama(const QJsonArray& messages) {
    QJsonArray out;
    for (const auto& entry : messages) {
        QJsonObject msg = entry.toObject();
        const QString role = msg.value("role").toString();

        if (role == "assistant") {
            // Ollama rejects null content
            if (!msg.contains("content") || msg.value("content").isNull())
                msg["content"] = QStringLiteral("");

            // OpenAI: arguments as JSON string -> Ollama: arguments as object
            if (msg.contains("tool_calls")) {
                QJsonArray converted;
                for (const auto& tc : msg.value("tool_calls").toArray()) {
                    const QJsonObject fn = tc.toObject().value("function").toObject();
                    QJsonObject cFn;
                    cFn["name"] = fn.value("name");
                    cFn["arguments"] = QJsonDocument::fromJson(
                        fn.value("arguments").toString().toUtf8()).object();
                    QJsonObject c;
                    c["function"] = cFn;
                    converted.append(c);
                }
                msg["tool_calls"] = converted;
            }
        } else if (role == "tool") {
            // Ollama tool messages only carry content
            msg.remove("tool_call_id");
        }

        out.append(msg);
    }
    return out;
}

void OllamaService::resetStreamState() {
    m_lineBuffer.clear();
    m_rawBody.clear();
    m_content.clear();
    m_toolCalls.clear();
    m_tokensUsed = 0;
    m_doneReceived = false;
    m_responseEmitted = false;
    m_errorEmitted = false;
}

void OllamaService::chatComplete(const QJsonArray& messages, const QJsonArray& tools) {
    resetStreamState();

    QNetworkRequest request;
    request.setUrl(QUrl(m_baseUrl + "/api/chat"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(300000); // local models can be slow

    QNetworkReply* reply = m_nam->post(request,
        QJsonDocument(buildRequestBody(messages, tools)).toJson());
    m_currentReply = reply;

    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() {
        if (m_currentReply != reply) return;
        onReadyRead();
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (m_currentReply == reply) m_currentReply = nullptr;
        handleReplyFinished(reply);
    });
}

void OllamaService::onReadyRead() {
    const QByteArray data = m_currentReply->readAll();
    m_rawBody.append(data);
    m_lineBuffer.append(data);
    processLineBuffer();
}

void OllamaService::processLineBuffer() {
    int newlineIdx;
    while ((newlineIdx = m_lineBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_lineBuffer.left(newlineIdx);
        m_lineBuffer.remove(0, newlineIdx + 1);
        line = line.trimmed();
        if (line.isEmpty()) continue;

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) continue;

        const QJsonObject json = doc.object();

        // Ollama error payload (model not found, etc.)
        if (json.contains("error")) {
            m_errorEmitted = true;
            emit errorOccurred(QStringLiteral("Ollama error: %1")
                .arg(json.value("error").toString()));
            continue;
        }

        const QJsonObject message = json.value("message").toObject();

        // Streamed content chunk
        const QString chunk = message.value("content").toString();
        if (!chunk.isEmpty()) {
            m_content += chunk;
            emit streamingChunk(chunk);
        }

        // Final line of a stream carries done=true (+ tool calls + usage)
        if (json.value("done").toBool()) {
            m_doneReceived = true;
            m_toolCalls = parseToolCalls(message);
            m_tokensUsed = json.value("eval_count").toInt()
                         + json.value("prompt_eval_count").toInt();
        }
    }
}

QVector<ToolCallInfo> OllamaService::parseToolCalls(const QJsonObject& message) {
    // Ollama tool_calls: {"function": {"name", "arguments": {...}}}
    // (older builds used flat {"name", "arguments"})
    QVector<ToolCallInfo> calls;
    const QJsonArray toolCalls = message.value("tool_calls").toArray();
    int index = 0;
    for (const auto& tc : toolCalls) {
        const QJsonObject tcObj = tc.toObject();
        QString name;
        QJsonObject arguments;
        if (tcObj.contains("function")) {
            const QJsonObject fn = tcObj.value("function").toObject();
            name = fn.value("name").toString();
            arguments = fn.value("arguments").toObject();
        } else {
            name = tcObj.value("name").toString();
            arguments = tcObj.value("arguments").toObject();
        }
        if (name.isEmpty()) continue;

        ToolCallInfo info;
        info.id = tcObj.value("id").toString();
        if (info.id.isEmpty()) info.id = QStringLiteral("call_%1").arg(index);
        info.name = name;
        info.arguments = arguments;
        calls.append(info);
        ++index;
    }
    return calls;
}

void OllamaService::emitStreamedResponse() {
    LLMResponse response;
    response.success = true;
    response.content = m_content;
    response.toolCalls = m_toolCalls;
    response.tokensUsed = m_tokensUsed;
    m_responseEmitted = true;
    emit responseReady(response);
}

void OllamaService::handleReplyFinished(QNetworkReply* reply) {
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError err = reply->error();
    const QString errString = reply->errorString();

    const QByteArray rest = reply->readAll();
    reply->deleteLater();

    if (!rest.isEmpty()) {
        m_rawBody.append(rest);
        m_lineBuffer.append(rest);
        processLineBuffer();
    }

    if (m_responseEmitted || m_errorEmitted) return;

    if (m_doneReceived) {
        emitStreamedResponse();
        return;
    }

    // No stream data: error body, plain non-stream JSON, or network failure
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(m_rawBody, &parseError);
    if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonObject json = doc.object();
        if (json.contains("error")) {
            emit errorOccurred(QStringLiteral("Ollama error (HTTP %1): %2")
                .arg(status).arg(json.value("error").toString()));
            return;
        }
        if (err == QNetworkReply::NoError && json.contains("message")) {
            // Server ignored the stream flag - treat as regular response
            LLMResponse response = parseResponse(json);
            if (response.success) {
                m_responseEmitted = true;
                emit responseReady(response);
            } else {
                emit errorOccurred(response.error.isEmpty()
                    ? QStringLiteral("Empty response from Ollama") : response.error);
            }
            return;
        }
    }

    if (err != QNetworkReply::NoError) {
        emit errorOccurred(QStringLiteral("Network error: %1").arg(errString));
        return;
    }

    emit errorOccurred("Empty or invalid response from Ollama");
}

LLMResponse OllamaService::parseResponse(const QJsonObject& json) {
    LLMResponse response;

    const QJsonObject message = json.value("message").toObject();
    response.content = message.value("content").toString();
    response.success = true;
    response.toolCalls = parseToolCalls(message);

    // Token usage reported on the final non-streamed message
    response.tokensUsed = json.value("eval_count").toInt()
                        + json.value("prompt_eval_count").toInt();

    return response;
}

void OllamaService::cancel() {
    if (m_currentReply) {
        m_currentReply->abort(); // finished handler performs cleanup
    }
}

} // namespace NexusAI
