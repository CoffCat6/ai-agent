#pragma once

#include "ITool.h"
#include "core/utils/Activity.h"
#include "core/utils/WebContent.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#include <atomic>

namespace NexusAI {

// Batch 20 (FEAT-2001/2002): GET-only web fetch with SSRF guarding and
// cooperative cancellation.
//
// Threading mirrors ShellExecTool (Batch 13): execute() runs on the
// QtConcurrent pool thread and owns the QNetworkAccessManager/reply; a
// sliced QEventLoop polls the atomic cancel flag between ~200 ms slices, so
// the GUI thread's cancel() only flips an atomic and the abort() happens on
// the very thread that owns the reply (ITool cancel contract - never touch
// the reply cross-thread, never block in cancel()).
//
// SSRF: the host is resolved before ANY connection; loopback / private /
// link-local addresses are rejected unless the opt-in escape hatch is set
// (motivation: local OpenAI-compatible gateways such as 127.0.0.1:8317).
// Redirects are NOT followed (ManualRedirectPolicy) - following one could
// bounce the request behind the guard. DNS-rebinding is outside the threat
// model for a local desktop app (declared in docs/7 Batch 20).
//
// Privacy: fetched content only flows into the tool result (→ model
// context); the usage trail records metadata (host/status/bytes) and never
// the body.
class NetworkFetchTool : public ITool {
public:
    ToolDefinition definition() const override {
        ToolDefinition def;
        def.name = "http_fetch";
        def.description =
            "Fetch a web page over HTTP/HTTPS (GET only) and return its text "
            "content (HTML stripped, size-capped). Privacy notice: fetched "
            "content will be sent to the current model service. Only use it "
            "when the current user message actually needs live web content.";
        def.level = ToolLevel::RequiresConfirm;
        def.autoEditSafe = true;   // 会话"自动编辑"模式放行
        def.readOnly = true;       // GET-only 检索：PlanOnly 下仍允许
        def.parameters = QJsonObject{
            {"type", "object"},
            {"properties", QJsonObject{
                {"url", QJsonObject{{"type", "string"},
                    {"description", "Absolute http(s) URL to fetch."}}},
                {"timeout_ms", QJsonObject{{"type", "integer"},
                    {"description", "Optional timeout in milliseconds (1000-60000, default 15000)."}}}}},
            {"required", QJsonArray{QJsonValue(QStringLiteral("url"))}}};
        return def;
    }

    // Escape hatch for loopback/private targets (local gateways). Wired from
    // SettingsService::allowLocalNetworkFetch via main.cpp applySettings.
    void setAllowPrivateTargets(bool allowed) { m_allowPrivateTargets.store(allowed); }

    // Batch 13 contract: GUI thread only - flips the atomic flag the sliced
    // execute() polls; never touches the network reply.
    void cancel() override { m_cancelRequested.store(true); }

    ToolResult execute(const QJsonObject& args) override {
        // Batch 21 (B3): clear any stale cancel from a previous call. cancel()
        // can land between two execute() invocations; without this reset the
        // first slice of the NEXT task would observe the leftover flag.
        m_cancelRequested.store(false);

        const QString urlText = args.value("url").toString().trimmed();
        if (urlText.isEmpty())
            return ToolResult::fail("url is required");
        int timeoutMs = args.value("timeout_ms").toInt(15000);

        // The fetch itself (SSRF guard, redirect policy, caps, slicing) lives
        // in WebContent::fetchUrl — shared with the knowledge-base URL import
        // so the security contracts cannot drift between the two consumers.
        const auto result = WebContent::fetchUrl(
            m_cancelRequested, urlText, m_allowPrivateTargets.load(), timeoutMs);

        switch (result.outcome) {
        case WebContent::FetchResult::Outcome::Cancelled:
        case WebContent::FetchResult::Outcome::TimedOut:
        case WebContent::FetchResult::Outcome::Failed:
            return ToolResult::fail(result.error);
        case WebContent::FetchResult::Outcome::TooLarge:
            // Partial body was read and already carries the note.
            Activity::usage(Activity::Kind::Network, QStringLiteral("http_fetch"),
                            QUrl(urlText).host(), 0,
                            QStringLiteral("status=%1 bytes=%2")
                                .arg(result.status).arg(result.bytes));
            return ToolResult::ok(result.text);
        case WebContent::FetchResult::Outcome::Redirected:
            return ToolResult::fail(QStringLiteral(
                "Fetch redirect (%1) at %2; resource moved to: %3")
                    .arg(result.status).arg(QUrl(urlText).host())
                    .arg(result.location));
        default:
            break;
        }

        Activity::usage(Activity::Kind::Network, QStringLiteral("http_fetch"),
                        QUrl(urlText).host(), 0,
                        QStringLiteral("status=%1 bytes=%2")
                            .arg(result.status).arg(result.bytes));
        return ToolResult::ok(result.text);
    }

private:
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<bool> m_allowPrivateTargets{false};
};

} // namespace NexusAI
