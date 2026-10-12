#pragma once

#include <QMetaType>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>
#include <QVector>

namespace NexusAI {
namespace Citation {

// docs/20 §5.1 (CITE-801): 引用溯源记号 "[文档名]" 的纯函数解析。
//
// 模型按系统提示约定在句子末尾追加 [文档名]；AssistantMessage 散文段用
// 同一份语义把记号切成「文本 / 引用」交替片段，引用片段渲染为可点击徽标。
//
// 识别规则（结构性，不校验文档是否真实存在 —— 存在性由 QML 渲染层按
// AgentVM.knowledgeDocuments 过滤，避免 [1]/[TODO] 之类的方括号误伤）：
//   1. '[' 起、同一行内 ']' 止，内容非空且 == trim 后的值（拒绝 "[ ]"、"[ a ]"）；
//   2. 内容不含 '[' / ']'（嵌套括号整组拒绝，不产生内部引用）；
//   3. 内容 ≤ 80 字符（超长方括号按普通文本处理）；
//   4. ']' 后紧跟 '(' 或 '[' 时按 markdown 链接/引用定义处理，不识别。
//      引用定义是 [ref][def] 这种**两组**方括号，只跳第一组会让第二组
//      （[def]）被当成引用 —— 所以这种情况要把紧跟的同组方括号一并吃掉；
//   5. '[' 前是 '!' 时按图片语法处理，不识别。
// 完整 provenance 记号（docs/25）：知识库工具结果教模型书写
// [doc:文档名#chunkId]；解析时归一化 —— docName 剥离 "doc:" 前缀与末尾
// "#<数字>"（取**最后一个** '#'，与 KnowledgeSearchTool 的约定一致：文档名
// 允许含 '#'），chunkIndex 记录 0 基分块号；简写 [文档名] 的 chunkIndex
// 为 -1。QML 渲染层的 isKnownCitation 按归一化后的 docName 匹配，两种
// 书写形态都能成为徽标。
// 拼接语义：引用片段只保留记号内容（方括号由徽标 chip 取代），其余片段
// 原文保留 —— 除记号定界符外无损，散文文字不会丢一个字。
//
// 这里是**唯一**实现：QML 通过下面的 CitationParser 单例调用它。曾有过一份
// 逐条镜像的 JS 实现（root.parseCitations），双份逻辑 + 双份用例已经造成
// 注释腐化，故已删除；单一事实源见 tests/test_highlighter.cpp。
struct Segment {
    QString text;      // 片段原文；isCitation 时 == 记号内容
    bool isCitation{false};
    QString docName;   // isCitation == true 时为归一化文档名（无 doc:/#N）
    int chunkIndex{-1}; // docs/25: 完整记号携带的 0 基分块号；简写为 -1
};

inline QVector<Segment> parse(const QString& text) {
    constexpr int kMaxNameChars = 80;
    const int n = text.size();
    QVector<Segment> out;
    int last = 0;   // 未切出的文本起点
    int i = 0;
    while (i < n) {
        if (text.at(i) != QLatin1Char('[')) { ++i; continue; }

        // 同一行内找 ']'，内容长度封顶 kMaxNameChars。
        int j = i + 1;
        while (j < n) {
            const QChar c = text.at(j);
            if (c == QLatin1Char('\n') || c == QLatin1Char(']')) break;
            if (j - i - 1 >= kMaxNameChars) break;
            ++j;
        }
        if (j >= n || text.at(j) != QLatin1Char(']')) { ++i; continue; }

        const QString content = text.mid(i + 1, j - i - 1);
        const bool nextOk = (j + 1 >= n)
            || (text.at(j + 1) != QLatin1Char('(')
                && text.at(j + 1) != QLatin1Char('['));
        const bool prevOk = (i == 0) || (text.at(i - 1) != QLatin1Char('!'));
        const bool ok = !content.isEmpty()
            && content == content.trimmed()
            && !content.contains(QLatin1Char('['))
            && !content.contains(QLatin1Char(']'))
            && nextOk && prevOk;
        if (!ok) {
            // 规则 4：']' 后紧跟 '[' 是引用定义（[ref][def]）的后半截。只跳到
            // j+1 会让紧跟的那组方括号被当成引用（CITE:def）—— 与规则 4 的意图
            // 相反。这里把同一行内紧跟的 ']' 也一并吃掉，整组按普通文本保留。
            // 注意 '[a] [b]'（中间有空格）不受影响：两组各自独立识别。
            if (j + 1 < n && text.at(j + 1) == QLatin1Char('[')) {
                int k = j + 2;
                while (k < n && text.at(k) != QLatin1Char(']')
                    && text.at(k) != QLatin1Char('\n')) {
                    ++k;
                }
                if (k < n && text.at(k) == QLatin1Char(']')) {
                    i = k + 1;
                    continue;
                }
            }
            i = j + 1;
            continue;
        }   // 整组跳过，不解析内部

        if (i > last)
            out.append({text.mid(last, i - last), false, {}, -1});
        Segment citation;
        citation.text = content;
        citation.isCitation = true;
        citation.docName = content;
        citation.chunkIndex = -1;
        // docs/25: [doc:文档名#chunkId] 完整记号归一化。取最后一个 '#'：
        // 其后须能解析为十进制整数才认定为 chunk 后缀，否则整体就是文档名
        // （文档名允许含 '#'，KnowledgeSearchTool 同一约定）。
        if (citation.docName.startsWith(QLatin1String("doc:"))) {
            const QString body = citation.docName.mid(4);
            const int hash = body.lastIndexOf(QLatin1Char('#'));
            if (hash >= 0 && hash + 1 < body.size()) {
                // 用 toInt(&ok) 而非手写 QChar::isDigit 循环：后者会接受
                // 全角/阿拉伯-印度数字，而 toInt 对它们解析失败会**静默返回
                // 0**，把引用误定位到第 1 个分块。ok 同时挡住溢出。
                bool isNumber = false;
                const int chunkId = body.mid(hash + 1).toInt(&isNumber);
                if (isNumber) {
                    citation.docName = body.left(hash);
                    citation.chunkIndex = chunkId;
                }
            }
        }
        out.append(citation);
        last = j + 1;
        i = j + 1;
    }
    if (last < n)
        out.append({text.mid(last), false, {}, -1});
    return out;
}

// 是否含至少一个记号（调用层可据此跳过 Flow 拆分布局）。
inline bool hasCitation(const QVector<Segment>& segments) {
    for (const Segment& s : segments)
        if (s.isCitation) return true;
    return false;
}

} // namespace Citation

// QML 单例封装。AssistantMessage.qml 里写作
//   CitationParser.parse(text)
// 返回 [{text, citation, name}]，与 C++ 侧 Citation::parse 一一对应。
// 纯逻辑留在上面那个命名空间里，C++ 测试（tests/test_highlighter.cpp）直接
// 调它，不依赖 QML 引擎。
class CitationParser : public QObject {
    Q_OBJECT
    QML_SINGLETON
    QML_ELEMENT

public:
    explicit CitationParser(QObject* parent = nullptr) : QObject(parent) {}

    static CitationParser* instance();
    static CitationParser* create(QQmlEngine*, QJSEngine*) {
        QJSEngine::setObjectOwnership(instance(), QJSEngine::CppOwnership);
        return instance();
    }

    // 把一段散文切成文本/引用交替片段。返回 QVariantList of QVariantMap：
    //   {text: 片段原文, citation: bool, name: 引用时的归一化文档名,
    //    chunk: 完整记号 [doc:name#N] 携带的 0 基分块号，简写为 -1}
    Q_INVOKABLE QVariantList parse(const QString& text) const {
        const QVector<Citation::Segment> segs = Citation::parse(text);
        QVariantList out;
        out.reserve(segs.size());
        for (const Citation::Segment& s : segs) {
            out.append(QVariantMap{
                {QStringLiteral("text"), s.text},
                {QStringLiteral("citation"), s.isCitation},
                {QStringLiteral("name"), s.docName},
                {QStringLiteral("chunk"), s.chunkIndex},
            });
        }
        return out;
    }

    // 是否含至少一个记号。
    Q_INVOKABLE bool hasCitation(const QVariantList& segments) const {
        for (const QVariant& v : segments) {
            if (v.toMap().value(QStringLiteral("citation")).toBool()) return true;
        }
        return false;
    }
};

inline CitationParser* CitationParser::instance() {
    static CitationParser s_instance;
    return &s_instance;
}

} // namespace NexusAI
