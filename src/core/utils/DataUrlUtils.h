#pragma once

#include <QString>
#include <QByteArray>

namespace NexusAI {

// Batch 30 (F6): data-URL parsing for multimodal image input. Images travel
// through the message protocol as "data:<mime>;base64,<payload>" strings;
// compression lives in the viewmodels layer (needs QImage/QtGui).
namespace DataUrlUtils {

// True when the string looks like "data:image/...;base64,...".
bool isImageDataUrl(const QString& dataUrl);

struct ParsedDataUrl {
    QString mimeType;      // e.g. "image/jpeg"
    QByteArray base64;     // payload without the header
    bool ok{false};
};

ParsedDataUrl parseDataUrl(const QString& dataUrl);

} // namespace DataUrlUtils

} // namespace NexusAI
