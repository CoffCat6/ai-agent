#pragma once

#include <QString>

namespace NexusAI {

// docs/20 §3.4（A4 拖拽附件）：把拖入/选中的本地文件按扩展名分派为图片、
// 文本或不受支持。纯字符串判定（不读盘、零 UI 依赖），便于单测覆盖；
// 实际读取与压缩在 viewmodels 层（AgentViewModel::loadImageDataUrl /
// loadTextFileAttachment）。
namespace AttachmentClassifier {

enum class Kind {
    Image,        // png/jpg/jpeg/webp/bmp/gif → 走图片压缩链路
    Text,         // 文本类扩展名 → 走文本附件链路
    Unsupported,  // 其余（含无扩展名）→ 拒绝并提示
};

// 按文件名的最后一个扩展名判定（大小写不敏感）。
Kind classify(const QString& fileName);

// 便于 QML/日志使用的字符串形式："image" / "text" / "unsupported"。
QString kindToString(Kind kind);

} // namespace AttachmentClassifier

} // namespace NexusAI
