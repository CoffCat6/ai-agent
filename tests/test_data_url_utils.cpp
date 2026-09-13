// Batch 30 (F6): DataUrlUtils — parsing/validation for multimodal image
// data URLs.
#include "core/utils/DataUrlUtils.h"

#include <QtTest>

using namespace NexusAI::DataUrlUtils;

class TestDataUrlUtils : public QObject {
    Q_OBJECT

private slots:
    void parseDataUrlSplitsMimeAndPayload() {
        const QString url = QStringLiteral("data:image/jpeg;base64,QUJD");
        const ParsedDataUrl parsed = parseDataUrl(url);
        QVERIFY(parsed.ok);
        QCOMPARE(parsed.mimeType, QStringLiteral("image/jpeg"));
        QCOMPARE(parsed.base64, QByteArray("QUJD"));   // raw payload, not decoded
        QVERIFY(isImageDataUrl(url));
    }

    void nonImageUrlsAreRejected() {
        QVERIFY(!isImageDataUrl(QStringLiteral("https://example.com/x.png")));
        QVERIFY(!isImageDataUrl(QStringLiteral("data:text/plain;base64,QUJD")));
        QVERIFY(!parseDataUrl(QStringLiteral("not a data url")).ok);
        QVERIFY(isImageDataUrl(QStringLiteral("data:image/png;base64,iVBOR")));
    }

    void emptyPayloadIsRejected() {
        QVERIFY(!parseDataUrl(QStringLiteral("data:image/png;base64,")).ok);
    }
};

QTEST_GUILESS_MAIN(TestDataUrlUtils)
#include "test_data_url_utils.moc"
