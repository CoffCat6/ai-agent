#pragma once

#include <QMetaType>
#include <QString>

namespace NexusAI {

// docs/20 §5.2 (workflow C2): PDF knowledge import.
//
// Only the "read the bytes" step of AgentViewModel::importDocument is
// replaced for .pdf files; dedup (content_hash), naming
// (uniqueDocumentName) and ingestion (startIngestion) are untouched. The
// extraction lives here, not in the view model, so tests/test_rag.cpp can
// drive it without instantiating AgentViewModel.
//
// LAYERING (code-review fix): QPdfDocument lives in Qt6::Pdf, which
// transitively depends on Qt6::Gui. Keeping this helper out of nexus_core
// preserves the "core 层零 UI 依赖" constraint (docs/13 §4.1) - nexus_core
// now links only Core/Sql/Concurrent/Network. It therefore lives in
// nexus_viewmodels (already Gui-dependent) and is reached by test_rag via
// that library.
enum class PdfExtractStatus {
    Ok,          // text extracted from every page
    OpenFailed,  // missing / unreadable file
    Encrypted,   // password-protected or unsupported security scheme
    Corrupted,   // not a PDF, damaged structure, or otherwise unparseable
    NoText       // parsed fine but has no text layer (e.g. a scan)
};

struct PdfExtractResult {
    PdfExtractStatus status{PdfExtractStatus::OpenFailed};
    QString text;    // page texts joined with '\n'; empty unless status == Ok
    QString detail;  // untranslated technical reason (logs / test output)

    bool ok() const { return status == PdfExtractStatus::Ok; }
};

// Concatenate getAllText() over every page of the document. Never throws
// and never aborts: an encrypted or damaged PDF yields a non-Ok status the
// caller turns into a user-facing message.
PdfExtractResult extractPdfText(const QString& filePath);

} // namespace NexusAI

// Lets QFutureWatcher<PdfExtractResult> carry the value across the worker
// thread that runs the (potentially slow) PDF parse.
Q_DECLARE_METATYPE(NexusAI::PdfExtractResult)
