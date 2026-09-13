#include "core/utils/DataUrlUtils.h"

namespace NexusAI {
namespace DataUrlUtils {

bool isImageDataUrl(const QString& dataUrl) {
    return dataUrl.startsWith(QStringLiteral("data:image/"))
        && dataUrl.contains(QStringLiteral(";base64,"));
}

ParsedDataUrl parseDataUrl(const QString& dataUrl) {
    ParsedDataUrl out;
    if (!isImageDataUrl(dataUrl)) return out;
    const int headerEnd = dataUrl.indexOf(QStringLiteral(";base64,"));
    out.mimeType = dataUrl.mid(5, headerEnd - 5);   // strip "data:" and ";base64,"
    out.base64 = dataUrl.mid(headerEnd + 8).toUtf8();
    out.ok = !out.mimeType.isEmpty() && !out.base64.isEmpty();
    return out;
}

} // namespace DataUrlUtils

} // namespace NexusAI
