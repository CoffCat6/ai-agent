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
        const QUrl url(urlText);
        if (!url.isValid() || url.host().isEmpty()
                || (url.scheme() != QLatin1String("http")
                    && url.scheme() != QLatin1String("https")))
            return ToolResult::fail("Only absolute http/https URLs are supported");

        int timeoutMs = args.value("timeout_ms").toInt(15000);
        timeoutMs = qBound(1000, timeoutMs, 60000);

        // SSRF guard: reject BEFORE any connection is attempted. The lookup is
        // asynchronous (Batch 21 / B2) so a slow resolver can neither block
        // past timeout_ms nor defeat cancel(); the guard still completes
        // before any network request is issued.
        if (!m_allowPrivateTargets.load()) {
            QString guardError;
            if (!hostIsAllowed(url.host(), timeoutMs, guardError))
                return ToolResult::fail(guardError);
        }

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
        // Batch 21 (B1): with ManualRedirectPolicy a 3xx finishes "successfully"
        // (NoError) but carries no useful body - returning an empty success
        // would hide the move from the model. Surface it as a failure and echo
        // the Location so the model can refetch the new address.
        if (outcome == Outcome::Finished && status >= 300 && status < 400) {
            // Prefer the raw Location response header (exactly what the
            // server sent); fall back to Qt's parsed/resolved attribute.
            QString location =
                QString::fromUtf8(reply->rawHeader("Location")).trimmed();
            if (location.isEmpty()) {
                const QVariant target = reply->attribute(
                    QNetworkRequest::RedirectionTargetAttribute);
                if (target.isValid() && target.toUrl().isValid())
                    location = target.toUrl().toString();
            }
            if (location.isEmpty())
                location = QStringLiteral("<no Location header>");
            return ToolResult::fail(QStringLiteral(
                "Fetch redirect (%1) at %2; resource moved to: %3")
                    .arg(status).arg(url.host()).arg(location));
        }
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

    // Non-routable / internal address ranges, IPv4+IPv6: loopback &
    // link-local via Qt, plus RFC1918, CGNAT (100.64/10), this-host 0.0.0.0,
    // limited broadcast 255.255.255.255 and IPv4 multicast (224/4).
    static bool isPrivateAddress(const QHostAddress& address) {
        if (address.isLoopback() || address.isLinkLocal()) return true;
        if (address.protocol() == QAbstractSocket::IPv4Protocol) {
            const quint32 v4 = address.toIPv4Address();
            return (v4 & 0xFF000000u) == 0x0A000000u      // 10/8
                || (v4 & 0xFFF00000u) == 0xAC100000u      // 172.16/12
                || (v4 & 0xFFFF0000u) == 0xC0A80000u      // 192.168/16
                || (v4 & 0xFFC00000u) == 0x64400000u      // 100.64/10 CGNAT
                || (v4 & 0xF0000000u) == 0xE0000000u      // 224/4 multicast
                || v4 == 0                                // 0.0.0.0
                || v4 == 0xFFFFFFFFu;                     // 255.255.255.255
        }
        if (address.protocol() == QAbstractSocket::IPv6Protocol) {
            const Q_IPV6ADDR v6 = address.toIPv6Address();
            return (v6[0] & 0xFE) == 0xFC;                // unique local fc00::/7
        }
        return false;
    }

    // Batch 21 (B2): asynchronous SSRF pre-check. Replaces the blocking
    // QHostInfo::fromName() - a slow resolver used to stall execute() (and
    // ignore both timeout_ms and cancel()) before the first network slice.
    // The lookup runs in an inner QEventLoop sliced by a timer so the existing
    // cancel/timeout contract applies to DNS too. Fails closed on private
    // targets, DNS failure or timeout.
    bool hostIsAllowed(const QString& host, int timeoutMs, QString& errorOut) {
        // Literal IPv4/IPv6 addresses short-circuit with NO DNS round-trip
        // (matches the old QHostInfo::fromName behavior). This keeps the
        // reject instant and avoids feeding a dotted-quad to an async
        // resolver backend that can stall until the timeout.
        QHostAddress literal;
        if (literal.setAddress(host)) {
            if (isPrivateAddress(literal)) {
                errorOut = QStringLiteral(
                    "rejected: internal network address (SSRF guard; enable "
                    "'allow local network fetch' in settings to override)");
                return false;
            }
            return true;
        }

        QEventLoop dnsLoop;
        QHostInfo info;
        bool done = false;

        // ITool is not a QObject, so give the async lookup its own context
        // object (lives on this pool thread for the duration of the call).
        QObject context;
        const int lookupId = QHostInfo::lookupHost(host, &context,
            [&](const QHostInfo& result) {
                info = result;
                done = true;
                dnsLoop.quit();
            });

        QElapsedTimer dnsClock;
        dnsClock.start();
        QTimer dnsSlicer;
        dnsSlicer.setInterval(kSliceMs);
        bool cancelled = false;
        bool timedOut = false;
        QObject::connect(&dnsSlicer, &QTimer::timeout, &dnsLoop, [&] {
            if (m_cancelRequested.exchange(false)) {
                cancelled = true;
                QHostInfo::abortHostLookup(lookupId);
                dnsLoop.quit();
                return;
            }
            if (dnsClock.elapsed() >= timeoutMs) {
                timedOut = true;
                QHostInfo::abortHostLookup(lookupId);
                dnsLoop.quit();
            }
        });
        dnsSlicer.start();
        if (!done) dnsLoop.exec();
        dnsSlicer.stop();

        if (cancelled) {
            errorOut = QStringLiteral("Fetch cancelled during DNS resolution");
            return false;
        }
        if (timedOut) {
            errorOut = QStringLiteral("DNS resolution timed out after %1 ms").arg(timeoutMs);
            return false;
        }
        if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
            errorOut = QStringLiteral("DNS resolution failed for %1: %2")
                           .arg(host, info.errorString());
            return false;
        }
        for (const QHostAddress& address : info.addresses()) {
            if (isPrivateAddress(address)) {
                errorOut = QStringLiteral(
                    "rejected: internal network address (SSRF guard; enable "
                    "'allow local network fetch' in settings to override)");
                return false;
            }
        }
        return true;
    }

    // Minimal HTML → text: drop script/style blocks, strip tags, decode the
    // handful of entities that matter. NOT a full HTML parser - good enough
    // for model-context readability, declared in docs/7 Batch 20.
    static QString processBody(const QByteArray& body) {
        // Try UTF-8 first; if the byte sequence is not valid UTF-8, fall
        // back to the system locale encoding so non-UTF-8 pages (e.g.
        // GBK on zh-CN Windows) are readable instead of mojibake.
        QString text = QString::fromUtf8(body);
        if (text.contains(QChar::ReplacementCharacter))
            text = QString::fromLocal8Bit(body);
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
