#include "viewmodels/PdfTextExtractor.h"

#include <QFileInfo>
#include <QStringList>
#include <QtPdf/QPdfDocument>

namespace NexusAI {

namespace {

QString detailFor(QPdfDocument::Error err) {
    switch (err) {
    case QPdfDocument::Error::None:
        return QStringLiteral("no error");
    case QPdfDocument::Error::Unknown:
        return QStringLiteral("unknown error");
    case QPdfDocument::Error::DataNotYetAvailable:
        return QStringLiteral("data not yet available");
    case QPdfDocument::Error::FileNotFound:
        return QStringLiteral("file not found");
    case QPdfDocument::Error::InvalidFileFormat:
        return QStringLiteral("invalid file format");
    case QPdfDocument::Error::IncorrectPassword:
        return QStringLiteral("password required");
    case QPdfDocument::Error::UnsupportedSecurityScheme:
        return QStringLiteral("unsupported encryption scheme");
    }
    return QStringLiteral("unknown error");
}

PdfExtractStatus statusFor(QPdfDocument::Error err) {
    switch (err) {
    case QPdfDocument::Error::IncorrectPassword:
    case QPdfDocument::Error::UnsupportedSecurityScheme:
        return PdfExtractStatus::Encrypted;
    case QPdfDocument::Error::FileNotFound:
        return PdfExtractStatus::OpenFailed;
    case QPdfDocument::Error::InvalidFileFormat:
    case QPdfDocument::Error::Unknown:
    case QPdfDocument::Error::DataNotYetAvailable:
        return PdfExtractStatus::Corrupted;
    case QPdfDocument::Error::None:
        break;
    }
    return PdfExtractStatus::OpenFailed;
}

} // namespace

PdfExtractResult extractPdfText(const QString& filePath) {
    PdfExtractResult result;

    if (!QFileInfo::exists(filePath)) {
        result.detail = QStringLiteral("no such file");
        return result;   // status: OpenFailed
    }

    QPdfDocument doc;
    // load() reports structural/encryption failures through its return
    // value; status()/error() mirror it, so consult both (a document that
    // reports Error via status() but Error::None from load() still fails).
    QPdfDocument::Error err = doc.load(filePath);
    if (err == QPdfDocument::Error::None && doc.status() == QPdfDocument::Status::Error)
        err = doc.error();
    if (err != QPdfDocument::Error::None) {
        result.status = statusFor(err);
        result.detail = detailFor(err);
        return result;
    }
    if (doc.status() != QPdfDocument::Status::Ready || doc.pageCount() <= 0) {
        // Not ready right after a successful load() means the parse never
        // completed - damaged or truncated input.
        result.status = PdfExtractStatus::Corrupted;
        result.detail = QStringLiteral("document not ready (status=%1, pages=%2)")
                            .arg(int(doc.status()))
                            .arg(doc.pageCount());
        return result;
    }

    QStringList pages;
    pages.reserve(doc.pageCount());
    for (int page = 0; page < doc.pageCount(); ++page)
        pages.append(doc.getAllText(page).text());
    result.text = pages.join(QLatin1Char('\n'));
    result.pageTexts = pages;   // docs/25: 页边界——chunk 页码映射的数据源

    if (result.text.trimmed().isEmpty()) {
        // Opens fine but carries no text layer (scanned image, empty file):
        // ingesting it would only waste an embedding round-trip on an
        // "empty document" error later.
        result.status = PdfExtractStatus::NoText;
        result.detail = QStringLiteral("no extractable text layer");
        result.text.clear();
        result.pageTexts.clear();   // 契约：仅 Ok 时携带页文本
        return result;
    }

    result.status = PdfExtractStatus::Ok;
    return result;
}

} // namespace NexusAI
