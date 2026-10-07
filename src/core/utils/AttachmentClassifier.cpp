#include "core/utils/AttachmentClassifier.h"

#include <QFileInfo>
#include <QSet>

namespace NexusAI {
namespace AttachmentClassifier {

namespace {

// 文本类扩展名：与 InputConsole 的 FileDialog nameFilters 保持一致，另收
// 常见配置/脚本后缀，方便拖拽直接作为文本附件。
const QSet<QString>& textSuffixes() {
    static const QSet<QString> suffixes = {
        QStringLiteral("txt"),  QStringLiteral("md"),   QStringLiteral("json"),
        QStringLiteral("csv"),  QStringLiteral("log"),  QStringLiteral("xml"),
        QStringLiteral("yml"),  QStringLiteral("yaml"), QStringLiteral("ini"),
        QStringLiteral("cpp"),  QStringLiteral("cc"),   QStringLiteral("cxx"),
        QStringLiteral("h"),    QStringLiteral("hpp"),  QStringLiteral("py"),
        QStringLiteral("js"),   QStringLiteral("ts"),   QStringLiteral("tsx"),
        QStringLiteral("jsx"),  QStringLiteral("html"), QStringLiteral("htm"),
        QStringLiteral("css"),  QStringLiteral("sh"),   QStringLiteral("bat"),
        QStringLiteral("ps1"),  QStringLiteral("toml"), QStringLiteral("conf"),
        QStringLiteral("cfg"),  QStringLiteral("sql"),  QStringLiteral("rs"),
        QStringLiteral("go"),   QStringLiteral("java"), QStringLiteral("kt"),
        QStringLiteral("c"),    QStringLiteral("rb"),   QStringLiteral("php"),
        QStringLiteral("vue"),  QStringLiteral("svelte"),
    };
    return suffixes;
}

const QSet<QString>& imageSuffixes() {
    static const QSet<QString> suffixes = {
        QStringLiteral("png"), QStringLiteral("jpg"),  QStringLiteral("jpeg"),
        QStringLiteral("webp"), QStringLiteral("bmp"), QStringLiteral("gif"),
    };
    return suffixes;
}

} // namespace

Kind classify(const QString& fileName) {
    // QFileInfo::suffix 取最后一个点后的部分；无扩展名时为空。
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    if (suffix.isEmpty()) return Kind::Unsupported;
    if (imageSuffixes().contains(suffix)) return Kind::Image;
    if (textSuffixes().contains(suffix)) return Kind::Text;
    return Kind::Unsupported;
}

QString kindToString(Kind kind) {
    switch (kind) {
    case Kind::Image:       return QStringLiteral("image");
    case Kind::Text:        return QStringLiteral("text");
    case Kind::Unsupported: return QStringLiteral("unsupported");
    }
    return QStringLiteral("unsupported");
}

} // namespace AttachmentClassifier

} // namespace NexusAI
