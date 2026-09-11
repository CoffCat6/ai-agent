// Batch 21 (A1) follow-up: unit coverage for the sampling-param normalizer
// (split out of test_settings so each executable runs a single QTest object).
#include "core/utils/SamplingParams.h"

#include <QtTest>

class TestSamplingParams : public QObject {
    Q_OBJECT
private slots:
    void optionalDoubleNormalizes() {
        using namespace NexusAI::SamplingParams;
        QVERIFY(!optionalDouble(QVariant(QStringLiteral("abc"))).isValid());  // garbage
        QVERIFY(!optionalDouble(QVariant(QStringLiteral("   "))).isValid());  // blank
        QVERIFY(!optionalDouble(QVariant(QStringLiteral("-1"))).isValid());   // negative
        QVERIFY(!optionalDouble(QVariant(QStringLiteral("1.2.3"))).isValid());
        QVERIFY(!optionalDouble(QVariant(QStringLiteral("1e400"))).isValid()); // overflow → inf
        QVERIFY(optionalDouble(QVariant(QStringLiteral("0.7"))).isValid());
        QCOMPARE(optionalDouble(QVariant(QStringLiteral("0.7"))).toDouble(), 0.7);
        QCOMPARE(optionalDouble(QVariant(QStringLiteral("0"))).toDouble(), 0.0);
        QCOMPARE(optionalDouble(QVariant(1.5)).toDouble(), 1.5);             // numeric QVariant
    }

    void optionalPositiveIntNormalizes() {
        using namespace NexusAI::SamplingParams;
        QVERIFY(!optionalPositiveInt(QVariant(QStringLiteral("abc"))).isValid());
        QVERIFY(!optionalPositiveInt(QVariant(QStringLiteral(""))).isValid());
        QVERIFY(!optionalPositiveInt(QVariant(QStringLiteral("0"))).isValid());
        QVERIFY(!optionalPositiveInt(QVariant(QStringLiteral("-5"))).isValid());
        QVERIFY(!optionalPositiveInt(QVariant(QStringLiteral("12.5"))).isValid()); // no fractions
        // Overflow beyond INT_MAX must normalize to "not set" instead of
        // wrapping through static_cast<int> (4294967296 → 0, 5e9 → 705032704).
        QVERIFY(!optionalPositiveInt(QVariant(QStringLiteral("2147483648"))).isValid());
        QVERIFY(!optionalPositiveInt(QVariant(QStringLiteral("4294967296"))).isValid());
        QVERIFY(!optionalPositiveInt(QVariant(QStringLiteral("9999999999"))).isValid());
        QCOMPARE(optionalPositiveInt(QVariant(QStringLiteral("2147483647"))).toInt(),
                 2147483647);   // INT_MAX boundary stays valid
        QVERIFY(optionalPositiveInt(QVariant(QStringLiteral("4096"))).isValid());
        QCOMPARE(optionalPositiveInt(QVariant(QStringLiteral("4096"))).toInt(), 4096);
        QCOMPARE(optionalPositiveInt(QVariant(8192)).toInt(), 8192);
    }
};

QTEST_GUILESS_MAIN(TestSamplingParams)
#include "test_sampling_params.moc"
