// docs/20 §3.4（A4 拖拽附件）：扩展名分派纯函数。
#include "core/utils/AttachmentClassifier.h"

#include <QtTest>

using namespace NexusAI::AttachmentClassifier;

class TestAttachmentClassifier : public QObject {
    Q_OBJECT

private slots:
    void imageExtensions() {
        QCOMPARE(classify(QStringLiteral("photo.png")), Kind::Image);
        QCOMPARE(classify(QStringLiteral("photo.JPG")), Kind::Image);
        QCOMPARE(classify(QStringLiteral("a.b.jpeg")), Kind::Image);
        QCOMPARE(classify(QStringLiteral("scan.WebP")), Kind::Image);
        QCOMPARE(classify(QStringLiteral("d.bmp")), Kind::Image);
        QCOMPARE(classify(QStringLiteral("anim.gif")), Kind::Image);
    }

    void textExtensions() {
        QCOMPARE(classify(QStringLiteral("notes.txt")), Kind::Text);
        QCOMPARE(classify(QStringLiteral("README.md")), Kind::Text);
        QCOMPARE(classify(QStringLiteral("data.json")), Kind::Text);
        QCOMPARE(classify(QStringLiteral("main.cpp")), Kind::Text);
        QCOMPARE(classify(QStringLiteral("script.PY")), Kind::Text);
        QCOMPARE(classify(QStringLiteral("conf.yml")), Kind::Text);
    }

    void unsupportedExtensions() {
        QCOMPARE(classify(QStringLiteral("archive.zip")), Kind::Unsupported);
        QCOMPARE(classify(QStringLiteral("binary.exe")), Kind::Unsupported);
        QCOMPARE(classify(QStringLiteral("movie.mp4")), Kind::Unsupported);
        // 无扩展名 / 尾部点
        QCOMPARE(classify(QStringLiteral("Makefile")), Kind::Unsupported);
        QCOMPARE(classify(QStringLiteral("trailing.")), Kind::Unsupported);
        QCOMPARE(classify(QString()), Kind::Unsupported);
    }

    void kindToStringStable() {
        QCOMPARE(kindToString(Kind::Image), QStringLiteral("image"));
        QCOMPARE(kindToString(Kind::Text), QStringLiteral("text"));
        QCOMPARE(kindToString(Kind::Unsupported), QStringLiteral("unsupported"));
    }
};

QTEST_GUILESS_MAIN(TestAttachmentClassifier)
#include "test_attachment_classifier.moc"
