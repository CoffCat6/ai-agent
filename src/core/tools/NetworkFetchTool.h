#pragma once

#include "ITool.h"
#include "core/utils/Activity.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
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
        const QString urlText = args.value("url").toString().trimmed();
        if (urlText.isEmpty())
            return ToolResult::fail("url is required");
        const QUrl url(urlText);
        if (!url.isValid() || url.host().isEmpty()
                || (url.scheme() != QLatin1String("http")
                    && url.scheme() != QLatin1String("https")))
            return ToolResult::fail("Only absolute http/https URLs are supported");

        int timeoutMs = args.value("timeout_ms").toInt(15000);
        timeoutMs = qBound(1000, timeoutMs, 60000);

        // SSRF guard: reject BEFORE any connection is attempted. Literal IP
        // hosts short-circuit in QHostInfo (no DNS round-trip); hostnames
        // resolve on this (pool) thread.
        if (!m_allowPrivateTargets.load() && isPrivateHost(url.host()))
            return ToolResult::fail(QStringLiteral(
                "rejected: internal network address (SSRF guard; enable "
                "'allow local network fetch' in settings to override)"));

        QNetworkAccessManager nam;
        QNetworkRequest request(url);
        // Redirects are not followed: they could bypass the SSRF guard.
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::ManualRedirectPolicy);
        QNetworkReply* reply = nam.get(request);

        QElapsedTimer clock;
        clock.start();
        enum class Outcome { Finished, Cancelled, TooLarge, TimedOut };
        Outcome outcome = Outcome::Finished;
        QEventLoop loop;
        QByteArray body;
        auto settle = [&](Outcome o) {
            outcome = o;
            reply->abort();   // same thread as the reply's owner; finished fires inline
            loop.quit();
        };
        QObject::connect(reply, &QNetworkReply::finished, &loop, [&] {
            if (outcome == Outcome::Finished) loop.quit();   // natural completion
        });
        QTimer slicer;
        slicer.setInterval(kSliceMs);
        QObject::connect(&slicer, &QTimer::timeout, &loop, [&] {
            if (m_cancelRequested.exchange(false)) { settle(Outcome::Cancelled); return; }
            body += reply->readAll();   // incremental: cap applies to the wire
            if (body.size() > kMaxBodyBytes) { settle(Outcome::TooLarge); return; }
            if (clock.elapsed() >= timeoutMs) settle(Outcome::TimedOut);
        });
        slicer.start();
        loop.exec();
        body += reply->readAll();
        slicer.stop();
        reply->deleteLater();
        // Fast responses can finish inside the first slice interval: re-check
        // the wire cap on the finished path so the limit has no race.
        if (outcome == Outcome::Finished && body.size() > kMaxBodyBytes)
            outcome = Outcome::TooLarge;

        if (outcome == Outcome::Cancelled)
            return ToolResult::fail(QStringLiteral("Fetch cancelled after %1 ms")
                                        .arg(clock.elapsed()));
        if (outcome == Outcome::TimedOut)
            return ToolResult::fail(QStringLiteral("Fetch timed out after %1 ms")
                                        .arg(timeoutMs));

        const qint64 status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toLongLong();
        if (outcome == Outcome::Finished && (status == 0 || status >= 400)) {
            const QString err = reply->error() != QNetworkReply::NoError
                ? reply->errorString()
                : QStringLiteral("HTTP %1").arg(status);
            return ToolResult::fail(QStringLiteral("Fetch failed (%1): %2")
                                        .arg(url.host()).arg(err));
        }

        QString text = processBody(body);
        QString note;
        if (outcome == Outcome::TooLarge)
            note = QStringLiteral("\n[truncated: response exceeded %1 bytes]")
                       .arg(kMaxBodyBytes);
        if (text.size() > kMaxResultChars)
            text = text.left(kMaxResultChars)
                 + QStringLiteral("\n…[truncated to %1 chars]").arg(kMaxResultChars);
        Activity::usage(Activity::Kind::Network, QStringLiteral("http_fetch"),
                        url.host(), 0,
                        QStringLiteral("status=%1 bytes=%2").arg(status).arg(body.size()));
        return ToolResult::ok(text + note);
    }

private:
    static constexpr int kSliceMs = 200;
    static constexpr int kMaxBodyBytes = 1024 * 1024;   // 1 MB wire cap
    static constexpr int kMaxResultChars = 6000;        // matches tool-result layering

    // Loopback / private (10/8, 172.16/12, 192.168/16) / link-local, IPv4+IPv6.
    static bool isPrivateAddress(const QHostAddress& address) {
        if (address.isLoopback() || address.isLinkLocal()) return true;
        if (address.protocol() == QAbstractSocket::IPv4Protocol) {
            const quint32 v4 = address.toIPv4Address();
            return (v4 & 0xFF000000u) == 0x0A000000u      // 10/8
                || (v4 & 0xFFF00000u) == 0xAC100000u      // 172.16/12
                || (v4 & 0xFFFF0000u) == 0xC0A80000u      // 192.168/16
                || v4 == 0;                               // 0.0.0.0
        }
        if (address.protocol() == QAbstractSocket::IPv6Protocol) {
            const Q_IPV6ADDR v6 = address.toIPv6Address();
            return (v6[0] & 0xFE) == 0xFC;                // unique local fc00::/7
        }
        return false;
    }

    static bool isPrivateHost(const QString& host) {
        const QHostInfo info = QHostInfo::fromName(host);
        if (info.error() != QHostInfo::NoError) return false;   // unresolvable → request fails on its own
        for (const QHostAddress& address : info.addresses())
            if (isPrivateAddress(address)) return true;
        return false;
    }

    // Minimal HTML → text: drop script/style blocks, strip tags, decode the
    // handful of entities that matter. NOT a full HTML parser - good enough
    // for model-context readability, declared in docs/7 Batch 20.
    static QString processBody(const QByteArray& body) {
        QString text = QString::fromUtf8(body);
        text.remove(QRegularExpression(QStringLiteral(
            "<(script|style)\\b[^>]*>.*?</\\1>"),
            QRegularExpression::CaseInsensitiveOption
                | QRegularExpression::DotMatchesEverythingOption));
        text.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
        text.replace(QStringLiteral("&amp;"), QStringLiteral("&"))
            .replace(QStringLiteral("&lt;"), QStringLiteral("<"))
            .replace(QStringLiteral("&gt;"), QStringLiteral(">"))
            .replace(QStringLiteral("&quot;"), QStringLiteral("\""))
            .replace(QStringLiteral("&#39;"), QStringLiteral("'"));
        text.replace(QRegularExpression(QStringLiteral("\\n{3,}")),
                     QStringLiteral("\n\n"));
        return text.trimmed();
    }

    std::atomic<bool> m_cancelRequested{false};
    std::atomic<bool> m_allowPrivateTargets{false};
};

} // namespace NexusAI
