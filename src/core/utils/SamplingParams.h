#pragma once

#include <QVariant>
#include <QString>

#include <cmath>
#include <limits>

namespace NexusAI {

// Batch 21 (A1): defensive normalization of the per-profile sampling
// parameters. QML validators block most bad input, but a profile can also be
// seeded from hand-edited QSettings or older data: anything that is not a
// valid number (e.g. "abc") must normalize to an INVALID QVariant so the
// services omit the JSON field ("not set") instead of silently sending 0 /
// clamping to 1 token. Empty / missing values mean "not set" as well.
//
// Parsing is locale-independent: profile values are stored as plain strings
// with an ASCII decimal point regardless of the system locale.

namespace SamplingParams {

// A finite non-negative double (temperature). Returns an invalid QVariant
// for empty, non-numeric, NaN/infinite, or negative input.
inline QVariant optionalDouble(const QVariant& v) {
    if (!v.isValid()) return {};
    if (v.metaType().id() == QMetaType::QString) {
        const QString text = v.toString().trimmed();
        if (text.isEmpty()) return {};
        bool ok = false;
        const double value = text.toDouble(&ok);
        // std::isfinite rejects "1e400" style overflow that toDouble reports
        // as ok=true with an inf result.
        if (!ok || value < 0.0 || !std::isfinite(value)) return {};
        return value;
    }
    bool ok = false;
    const double value = v.toDouble(&ok);
    if (!ok || value < 0.0 || !std::isfinite(value)) return {};
    return value;
}

// A positive integer (maxOutputTokens / numCtx). Returns an invalid QVariant
// for empty, non-numeric, fractional, zero, negative, or out-of-int-range
// input - a value above INT_MAX must NOT wrap through static_cast<int>
// (e.g. 4294967296 would silently become 0 and be sent as max_tokens: 0).
inline QVariant optionalPositiveInt(const QVariant& v) {
    if (!v.isValid()) return {};
    qlonglong value = 0;
    bool ok = false;
    if (v.metaType().id() == QMetaType::QString) {
        const QString text = v.toString().trimmed();
        if (text.isEmpty()) return {};
        value = text.toLongLong(&ok);
        if (!ok || value <= 0) return {};
    } else {
        value = v.toLongLong(&ok);
        if (!ok || value <= 0) return {};
    }
    if (value > std::numeric_limits<int>::max()) return {};
    return static_cast<int>(value);
}

} // namespace SamplingParams
} // namespace NexusAI
