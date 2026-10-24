#pragma once

#include <QElapsedTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QString>
#include <QStringList>

#include <atomic>

namespace NexusAI::WebContent {

// Shared web-fetch plumbing for the two consumers that need raw page text:
//   - NetworkFetchTool (model-facing http_fetch tool)
//   - knowledge-base URL import (AgentViewModel::importFromUrl)
//
// Extracted from NetworkFetchTool so the two paths cannot drift: the SSRF
// guard, the wire cap, the redirect policy and the HTML stripping are security
// and quality contracts, not per-feature details.
//
// Threading: fetchUrl() runs its own sliced QEventLoop (same proven pattern
// as the tool) and must therefore be called from a thread that may own an
// event loop — the GUI thread for the import path, the QtConcurrent pool for
// the tool. cancel is an atomic so a GUI-side cancel() never touches the
// reply cross-thread.

struct FetchResult {
    enum class Outcome { Finished, Cancelled, TooLarge, TimedOut, Failed, Redirected };

    Outcome outcome{Outcome::Finished};
    QString text;         // HTML stripped (or raw body for non-HTML types)
    QString error;        // human-readable on failure
    QString location;     // redirect target when outcome == Redirected
    qint64 status{0};
    qint64 bytes{0};      // wire bytes received
};

// Non-routable / internal address ranges, IPv4+IPv6: loopback & link-local
// via Qt, plus RFC1918, CGNAT (100.64/10), this-host 0.0.0.0, limited
// broadcast 255.255.255.255 and IPv4 multicast (224/4).
inline bool isPrivateAddress(const QHostAddress& address) {
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

// Batch 21 (B2) pattern: asynchronous SSRF pre-check sliced by a timer, so a
// slow resolver can neither block past timeoutMs nor defeat cancel. Fails
// closed on private targets, DNS failure or timeout.
inline bool hostIsAllowed(std::atomic<bool>& cancelFlag, const QString& host,
                          int timeoutMs, QString& errorOut) {
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
    dnsSlicer.setInterval(200);
    bool cancelled = false;
    bool timedOut = false;
    QObject::connect(&dnsSlicer, &QTimer::timeout, &dnsLoop, [&] {
        if (cancelFlag.exchange(false)) {
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
// handful of entities that matter. NOT a full HTML parser — navigation and
// chrome noise survive; acceptable for retrieval, declared here so nobody
// expects parser-grade extraction.
inline QString htmlToText(const QByteArray& body) {
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

// GET one http(s) URL with SSRF guard, manual redirect policy, wire cap and
// sliced cancellation. allowPrivateTargets is the settings escape hatch
// (local gateways). maxBodyBytes caps the wire; maxResultChars caps the
// returned text.
inline FetchResult fetchUrl(std::atomic<bool>& cancelFlag, const QString& urlText,
                            bool allowPrivateTargets, int timeoutMs = 15000,
                            qint64 maxBodyBytes = 1024 * 1024,
                            int maxResultChars = 6000) {
    FetchResult result;
    const QUrl url(urlText);
    if (!url.isValid() || url.host().isEmpty()
            || (url.scheme() != QLatin1String("http")
                && url.scheme() != QLatin1String("https"))) {
        result.outcome = FetchResult::Outcome::Failed;
        result.error = QStringLiteral("Only absolute http/https URLs are supported");
        return result;
    }

    timeoutMs = qBound(1000, timeoutMs, 60000);

    // SSRF guard BEFORE any connection (async lookup; see hostIsAllowed).
    if (!allowPrivateTargets) {
        QString guardError;
        if (!hostIsAllowed(cancelFlag, url.host(), timeoutMs, guardError)) {
            result.outcome = FetchResult::Outcome::Failed;
            result.error = guardError;
            return result;
        }
    }

    QNetworkAccessManager nam;
    QNetworkRequest request(url);
    // Redirects are not followed: they could bounce the request behind the
    // SSRF guard. Surfaced as Redirected with the Location.
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
    slicer.setInterval(200);
    QObject::connect(&slicer, &QTimer::timeout, &loop, [&] {
        if (cancelFlag.exchange(false)) { settle(Outcome::Cancelled); return; }
        body += reply->readAll();   // incremental: cap applies to the wire
        if (body.size() > maxBodyBytes) { settle(Outcome::TooLarge); return; }
        if (clock.elapsed() >= timeoutMs) settle(Outcome::TimedOut);
    });
    slicer.start();
    loop.exec();
    body += reply->readAll();
    slicer.stop();
    result.status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toLongLong();
    result.bytes = body.size();
    reply->deleteLater();
    // Fast responses can finish inside the first slice interval: re-check the
    // wire cap on the finished path so the limit has no race.
    if (outcome == Outcome::Finished && body.size() > maxBodyBytes)
        outcome = Outcome::TooLarge;

    switch (outcome) {
    case Outcome::Cancelled:
        result.outcome = FetchResult::Outcome::Cancelled;
        result.error = QStringLiteral("Fetch cancelled after %1 ms").arg(clock.elapsed());
        return result;
    case Outcome::TimedOut:
        result.outcome = FetchResult::Outcome::TimedOut;
        result.error = QStringLiteral("Fetch timed out after %1 ms").arg(timeoutMs);
        return result;
    case Outcome::TooLarge:
        // NOT a failure: the first maxBodyBytes were read, and the tool-side
        // contract (test httpFetchSizeCapTruncates) returns them with a
        // truncation note. Callers needing all-or-nothing check outcome.
        result.outcome = FetchResult::Outcome::TooLarge;
        result.text = htmlToText(body).left(maxResultChars)
            + QStringLiteral("\n[truncated: response exceeded %1 bytes]")
                  .arg(maxBodyBytes);
        return result;
    default:
        break;
    }

    // With ManualRedirectPolicy a 3xx finishes "successfully" (NoError) but
    // carries no useful body — surfacing it hides nothing from the caller.
    if (result.status >= 300 && result.status < 400) {
        result.outcome = FetchResult::Outcome::Redirected;
        result.location = QString::fromUtf8(reply->rawHeader("Location")).trimmed();
        if (result.location.isEmpty()) {
            const QVariant target =
                reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
            if (target.isValid() && target.toUrl().isValid())
                result.location = target.toUrl().toString();
        }
        if (result.location.isEmpty())
            result.location = QStringLiteral("<no Location header>");
        return result;
    }
    if (result.status == 0 || result.status >= 400) {
        result.outcome = FetchResult::Outcome::Failed;
        result.error = reply->error() != QNetworkReply::NoError
            ? reply->errorString()
            : QStringLiteral("HTTP %1").arg(result.status);
        return result;
    }

    result.outcome = FetchResult::Outcome::Finished;
    QString text = htmlToText(body);
    if (text.size() > maxResultChars)
        text = text.left(maxResultChars)
             + QStringLiteral("\n…[truncated to %1 chars]").arg(maxResultChars);
    result.text = text;
    return result;
}

} // namespace NexusAI::WebContent
