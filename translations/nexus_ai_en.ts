<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="en_US" sourcelanguage="zh_CN">
<context>
    <name>ActivityLogPage</name>
    <message>
        <location filename="../src/ui/pages/ActivityLogPage.qml" line="18"/>
        <source>活动记录页</source>
        <translation>Activity Log Page</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ActivityLogPage.qml" line="19"/>
        <source>浏览用户操作、智能体活动与网络请求的使用轨迹</source>
        <translation>Browse the usage trail of user operations, agent activities and network requests</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ActivityLogPage.qml" line="54"/>
        <source>用户</source>
        <translation>User</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ActivityLogPage.qml" line="55"/>
        <location filename="../src/ui/pages/ActivityLogPage.qml" line="139"/>
        <source>智能体</source>
        <translation>Agent</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ActivityLogPage.qml" line="56"/>
        <location filename="../src/ui/pages/ActivityLogPage.qml" line="139"/>
        <source>网络</source>
        <translation>Network</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ActivityLogPage.qml" line="58"/>
        <source>未知</source>
        <translation>Unknown</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ActivityLogPage.qml" line="91"/>
        <source>活动记录</source>
        <translation>Activity Log</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ActivityLogPage.qml" line="92"/>
        <source>浏览操作与网络请求的使用轨迹。仅记录操作元数据（操作类型、对象、结果），不包含对话内容、文档内容与 API 密钥。</source>
        <translation>Browse the usage trail of operations and network requests. Only operation metadata (action, target, result) is recorded; conversation content, document content and API keys are never included.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ActivityLogPage.qml" line="139"/>
        <source>全部</source>
        <translation>All</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ActivityLogPage.qml" line="139"/>
        <source>用户操作</source>
        <translation>User Actions</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ActivityLogPage.qml" line="142"/>
        <source>活动类别筛选</source>
        <translation>Activity category filter</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ActivityLogPage.qml" line="147"/>
        <source>过滤操作 / 对象 / 说明…</source>
        <translation>Filter by action / target / detail…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ActivityLogPage.qml" line="160"/>
        <source>过滤活动记录关键词</source>
        <translation>Filter activity log by keyword</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ActivityLogPage.qml" line="164"/>
        <source>共 %1 条</source>
        <translation>%1 entries total</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ActivityLogPage.qml" line="170"/>
        <source>刷新</source>
        <translation>Refresh</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ActivityLogPage.qml" line="172"/>
        <source>刷新活动记录</source>
        <translation>Refresh activity log</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ActivityLogPage.qml" line="194"/>
        <source>暂无活动记录</source>
        <translation>No activity records yet</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ActivityLogPage.qml" line="195"/>
        <source>无匹配的活动记录</source>
        <translation>No matching activities</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ActivityLogPage.qml" line="217"/>
        <source>—</source>
        <translation>—</translation>
    </message>
</context>
<context>
    <name>AgentBehaviorPage</name>
    <message>
        <location filename="../src/ui/pages/AgentBehaviorPage.qml" line="13"/>
        <source>智能体行为页</source>
        <translation>Agent behavior page</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentBehaviorPage.qml" line="15"/>
        <source>智能体行为</source>
        <translation>Agent Behavior</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentBehaviorPage.qml" line="16"/>
        <source>控制单次任务如何运行；修改即时保存。</source>
        <translation>Controls how a single task runs; changes are saved immediately.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentBehaviorPage.qml" line="24"/>
        <source>任务预算</source>
        <translation>Task Budget</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentBehaviorPage.qml" line="25"/>
        <source>限制一次任务里模型能走多远、跑多久；复杂任务可调高，但通常会增加耗时和 token 消耗。</source>
        <translation>Limits how far and how long the model can go in one task; raise it for complex tasks, at the cost of time and tokens.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentBehaviorPage.qml" line="30"/>
        <source>单次任务步数预算</source>
        <translation>Step budget per task</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentBehaviorPage.qml" line="49"/>
        <source>一次任务里模型最多进行多少轮「思考 → 工具调用」（3–1000）。用完会强制基于已有信息作答。</source>
        <translation>Maximum rounds of think → tool call per task (3–1000). When exhausted, the model must answer from the information it has.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentBehaviorPage.qml" line="59"/>
        <source>单次任务超时（分钟）</source>
        <translation>Task timeout (minutes)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentBehaviorPage.qml" line="60"/>
        <source>任务运行超过此时长会停止并基于已收集的信息作答。需要多次网络调用的任务可以适当调高。</source>
        <translation>The task stops after this long and answers from the information gathered. Raise it for tasks that need many network calls.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentBehaviorPage.qml" line="68"/>
        <source>单次任务超时，分钟</source>
        <translation>Task timeout in minutes</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentBehaviorPage.qml" line="75"/>
        <source>系统提示词</source>
        <translation>System Prompt</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentBehaviorPage.qml" line="76"/>
        <source>留空使用内置提示词；修改即时生效，内容随请求发送给当前模型服务。</source>
        <translation>Leave empty to use the built-in prompt; changes take effect immediately and are sent with each request.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentBehaviorPage.qml" line="84"/>
        <source>留空 = 内置提示词</source>
        <translation>Empty = built-in prompt</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentBehaviorPage.qml" line="110"/>
        <source>自定义系统提示词</source>
        <translation>Custom System Prompt</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentBehaviorPage.qml" line="114"/>
        <source>%1 字符</source>
        <translation>%1 chars</translation>
    </message>
</context>
<context>
    <name>AgentChatPage</name>
    <message>
        <location filename="../src/ui/pages/AgentChatPage.qml" line="33"/>
        <source>AI 对话页面</source>
        <translation>AI chat page</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentChatPage.qml" line="172"/>
        <source>发送一条消息，开始与 NexusAI 对话</source>
        <translation>Send a message to start chatting with NexusAI</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentChatPage.qml" line="173"/>
        <source>先连接一个模型，即可开始对话。你的输入会保留在这里。</source>
        <translation>Connect a model to start chatting. Your draft will stay here.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentChatPage.qml" line="184"/>
        <source>配置模型</source>
        <translation>Configure model</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentChatPage.qml" line="222"/>
        <source>从模板开始</source>
        <translation>Start from a template</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentChatPage.qml" line="247"/>
        <source>从会话模板开始</source>
        <translation>Start from a session template</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentChatPage.qml" line="286"/>
        <source>有待确认 · %1</source>
        <translation>Pending · %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentChatPage.qml" line="304"/>
        <source>有待确认的工具操作，点击查看</source>
        <translation>Pending tool confirmation, click to view</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/AgentChatPage.qml" line="339"/>
        <source>回到底部</source>
        <translation>Scroll to bottom</translation>
    </message>
</context>
<context>
    <name>AgentTraceNode</name>
    <message>
        <source>追踪节点</source>
        <translation type="vanished">Trace node</translation>
    </message>
    <message>
        <source>已折叠 · %1 字符，点击展开</source>
        <translation type="vanished">Collapsed · %1 chars, click to expand</translation>
    </message>
    <message>
        <source>拒绝</source>
        <translation type="vanished">Deny</translation>
    </message>
    <message>
        <source>允许</source>
        <translation type="vanished">Allow</translation>
    </message>
</context>
<context>
    <name>AssistantMessage</name>
    <message>
        <location filename="../src/ui/components/AssistantMessage.qml" line="349"/>
        <source>打开引用文档 %1，定位到分块 %2</source>
        <translation>Open cited document %1, located at chunk %2</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AssistantMessage.qml" line="351"/>
        <source>打开引用文档 %1</source>
        <translation>Open cited document %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AssistantMessage.qml" line="417"/>
        <location filename="../src/ui/components/AssistantMessage.qml" line="437"/>
        <source>从此处分支</source>
        <translation>Branch from here</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AssistantMessage.qml" line="438"/>
        <source>把到这里为止的对话复制成一个新会话，在新会话里继续</source>
        <translation>Copy the conversation up to this point into a new chat and continue there</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AssistantMessage.qml" line="460"/>
        <location filename="../src/ui/components/AssistantMessage.qml" line="480"/>
        <source>重新生成</source>
        <translation>Regenerate</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AssistantMessage.qml" line="481"/>
        <source>丢弃当前回答并重新生成</source>
        <translation>Discard the current answer and regenerate</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AssistantMessage.qml" line="490"/>
        <source>助手回复</source>
        <translation>Assistant reply</translation>
    </message>
</context>
<context>
    <name>AttachmentChip</name>
    <message>
        <location filename="../src/ui/components/AttachmentChip.qml" line="99"/>
        <source>移除附件</source>
        <translation>Remove attachment</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AttachmentChip.qml" line="104"/>
        <source>图片附件</source>
        <translation>Image attachment</translation>
    </message>
</context>
<context>
    <name>AuditDialog</name>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="53"/>
        <source>工具执行</source>
        <translation>Tool execution</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="54"/>
        <source>工具已批准</source>
        <translation>Tool approved</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="55"/>
        <source>工具已拒绝</source>
        <translation>Tool declined</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="56"/>
        <source>工具已拦截</source>
        <translation>Tool blocked</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="57"/>
        <source>未知工具</source>
        <translation>Unknown tool</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="70"/>
        <source>审计日志对话框</source>
        <translation>Audit log dialog</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="71"/>
        <source>查看工具执行审计记录，支持筛选与复制</source>
        <translation>View tool execution audit records, supports filtering and copy</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="77"/>
        <source>审计日志</source>
        <translation>Audit Log</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="92"/>
        <source>全部</source>
        <translation>All</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="93"/>
        <source>执行</source>
        <translation>Execution</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="94"/>
        <location filename="../src/ui/components/AuditDialog.qml" line="194"/>
        <source>允许</source>
        <translation>Allowed</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="95"/>
        <source>拒绝/拦截</source>
        <translation>Denied/Blocked</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="130"/>
        <source>显示 %1 / %2 条</source>
        <translation>Showing %1 / %2</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="156"/>
        <source>暂无记录</source>
        <translation>No records yet</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="157"/>
        <source>当前筛选条件下暂无记录</source>
        <translation>No records match the current filter</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="193"/>
        <source>成功</source>
        <translation>OK</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="193"/>
        <source>失败</source>
        <translation>Failed</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="195"/>
        <source>拒绝</source>
        <translation>Deny</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="196"/>
        <source>拦截</source>
        <translation>Block</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="197"/>
        <source>未知</source>
        <translation>Unknown</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="288"/>
        <source>刷新</source>
        <translation>Refresh</translation>
    </message>
    <message>
        <location filename="../src/ui/components/AuditDialog.qml" line="300"/>
        <source>关闭</source>
        <translation>Close</translation>
    </message>
</context>
<context>
    <name>BarCompareChart</name>
    <message>
        <location filename="../src/ui/components/charts/BarCompareChart.qml" line="50"/>
        <source>次</source>
        <translation>calls</translation>
    </message>
    <message>
        <location filename="../src/ui/components/charts/BarCompareChart.qml" line="50"/>
        <source>Token</source>
        <translation>Token</translation>
    </message>
    <message>
        <location filename="../src/ui/components/charts/BarCompareChart.qml" line="102"/>
        <source>暂无数据</source>
        <translation>No data</translation>
    </message>
    <message>
        <location filename="../src/ui/components/charts/BarCompareChart.qml" line="105"/>
        <location filename="../src/ui/components/charts/BarCompareChart.qml" line="322"/>
        <source>%1: %2 %3</source>
        <translation>%1: %2 %3</translation>
    </message>
    <message>
        <location filename="../src/ui/components/charts/BarCompareChart.qml" line="107"/>
        <source>共 %1 个模型，最高 %2 %3</source>
        <translation>%1 model(s), peak %2 %3</translation>
    </message>
    <message>
        <location filename="../src/ui/components/charts/BarCompareChart.qml" line="211"/>
        <source>该范围内暂无数据</source>
        <translation>No data in this range</translation>
    </message>
    <message>
        <location filename="../src/ui/components/charts/BarCompareChart.qml" line="339"/>
        <source>模型用量对比柱状图</source>
        <translation>Model usage comparison bar chart</translation>
    </message>
</context>
<context>
    <name>CategoryNav</name>
    <message>
        <location filename="../src/ui/components/CategoryNav.qml" line="375"/>
        <source>当前选中</source>
        <translation>Currently selected</translation>
    </message>
</context>
<context>
    <name>ChangeTree</name>
    <message>
        <location filename="../src/ui/components/ChangeTree.qml" line="203"/>
        <source>折叠或展开目录 %1</source>
        <translation>Collapse or expand folder %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ChangeTree.qml" line="285"/>
        <source>原文件过大，仅记录了路径，无法展示内容差异或撤销</source>
        <translation>Original file too large; only its path was recorded, so no diff or undo is available.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ChangeTree.qml" line="325"/>
        <source>已撤销</source>
        <translation>Undone</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ChangeTree.qml" line="325"/>
        <source>已确认</source>
        <translation>Accepted</translation>
    </message>
</context>
<context>
    <name>CodeBlock</name>
    <message>
        <location filename="../src/ui/components/CodeBlock.qml" line="114"/>
        <source>代码块（%1）</source>
        <translation>Code block (%1)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/CodeBlock.qml" line="115"/>
        <source>代码块</source>
        <translation>Code block</translation>
    </message>
</context>
<context>
    <name>ContextPanel</name>
    <message>
        <location filename="../src/ui/panels/ContextPanel.qml" line="18"/>
        <source>上下文面板</source>
        <translation>Context panel</translation>
    </message>
    <message>
        <source>文本文件 (*.txt *.md *.json *.csv *.log)</source>
        <translation type="vanished">Text files (*.txt *.md *.json *.csv *.log)</translation>
    </message>
    <message>
        <source>所有文件 (*)</source>
        <translation type="vanished">All files (*)</translation>
    </message>
    <message>
        <source>支持的文件 (*.txt *.md *.json *.csv *.log *.pdf)</source>
        <translation type="vanished">Supported files (*.txt *.md *.json *.csv *.log *.pdf)</translation>
    </message>
    <message>
        <source>审阅 · %1</source>
        <translation type="vanished">Review · %1</translation>
    </message>
    <message>
        <source>审阅</source>
        <translation type="vanished">Review</translation>
    </message>
    <message>
        <source>查看本会话的文件改动审阅</source>
        <translation type="vanished">Review this session&apos;s file changes</translation>
    </message>
    <message>
        <source>本次会话暂无可审阅的文件改动。
智能体写入或编辑文件后，这里会按目录列出改动；
选中文件即可在下方查看内容差异。</source>
        <translation type="vanished">No file changes to review in this session yet.
After the agent writes or edits files they are listed here by folder;
select a file to view its content diff below.</translation>
    </message>
    <message>
        <source>知识库 · %1</source>
        <translation type="vanished">Knowledge Base · %1</translation>
    </message>
    <message>
        <source>知识库</source>
        <translation type="vanished">Knowledge Base</translation>
    </message>
    <message>
        <location filename="../src/ui/panels/ContextPanel.qml" line="75"/>
        <source>文件</source>
        <translation>Files</translation>
    </message>
    <message>
        <location filename="../src/ui/panels/ContextPanel.qml" line="81"/>
        <source>浏览项目文件</source>
        <translation>Browse project files</translation>
    </message>
    <message>
        <location filename="../src/ui/panels/ContextPanel.qml" line="88"/>
        <source>Git</source>
        <translation>Git</translation>
    </message>
    <message>
        <location filename="../src/ui/panels/ContextPanel.qml" line="94"/>
        <source>查看 Git 管理与历史</source>
        <translation>View Git management and history</translation>
    </message>
    <message>
        <source>当前会话未关联项目</source>
        <translation type="vanished">This session is not tied to a project</translation>
    </message>
    <message>
        <source>Git 管理与文件浏览在项目会话中可用；
知识库已移至左下角工具条。</source>
        <translation type="vanished">Git management and file browsing are available in project sessions;\nthe knowledge base moved to the toolbar at the bottom left.</translation>
    </message>
    <message>
        <source>查看知识库文档</source>
        <translation type="vanished">View knowledge base documents</translation>
    </message>
    <message>
        <source>本次会话暂无可审阅的文件改动。
智能体写入或编辑文件后，这里会按目录列出改动。</source>
        <translation type="vanished">No file changes to review in this session.
Files written or edited by the agent are listed here by folder.</translation>
    </message>
    <message>
        <source>文档内容会随提问发送给当前模型服务；如需阻止，请在设置中开启“仅本地模式”</source>
        <translation type="vanished">Document content is sent to the current model service with your questions; to prevent this, enable &quot;Local-only mode&quot; in Settings</translation>
    </message>
    <message>
        <source>批量导入 %1/%2：正在取消…</source>
        <translation type="vanished">Batch import %1/%2: cancelling…</translation>
    </message>
    <message>
        <source>批量导入 %1/%2：%3</source>
        <translation type="vanished">Batch import %1/%2: %3</translation>
    </message>
    <message>
        <source>已嵌入 %1/%2</source>
        <translation type="vanished">Embedded %1/%2</translation>
    </message>
    <message>
        <source>导入中…</source>
        <translation type="vanished">Importing…</translation>
    </message>
    <message>
        <source>正在取消（等待当前文件完成）</source>
        <translation type="vanished">Cancelling (waiting for the current file to finish)</translation>
    </message>
    <message>
        <source>取消批量导入（未开始的文件将跳过）</source>
        <translation type="vanished">Cancel batch import (pending files will be skipped)</translation>
    </message>
    <message>
        <source>导入文档（可多选）</source>
        <translation type="vanished">Import documents (multi-select)</translation>
    </message>
    <message>
        <source>重试失败的导入（%1）</source>
        <translation type="vanished">Retry failed imports (%1)</translation>
    </message>
    <message>
        <source>重试失败的批量导入</source>
        <translation type="vanished">Retry failed batch imports</translation>
    </message>
    <message>
        <source>%1 段 · %2 字符</source>
        <translation type="vanished">%1 chunks · %2 chars</translation>
    </message>
    <message>
        <source>移除文档</source>
        <translation type="vanished">Remove document</translation>
    </message>
    <message>
        <source>导入文档</source>
        <translation type="vanished">Import Document</translation>
    </message>
    <message>
        <source>导入文本或 PDF 文件（.txt / .md / .pdf …），
模型即可检索其内容回答问题</source>
        <translation type="vanished">Import text or PDF files (.txt / .md / .pdf …),
and the model can retrieve their content to answer questions</translation>
    </message>
    <message>
        <source>导入文本文件（.txt / .md …），
模型即可检索其内容回答问题</source>
        <translation type="vanished">Import text files (.txt / .md …),
and the model can retrieve their content to answer questions</translation>
    </message>
    <message>
        <source>降级</source>
        <translation type="vanished">Fallback</translation>
    </message>
    <message>
        <source>导入时嵌入服务不可用，已使用本地降级嵌入，检索质量有限。可点击右侧刷新按钮重新嵌入。</source>
        <translation type="vanished">The embedding service was unavailable during import; a local fallback embedding was used with limited retrieval quality. Click the refresh button on the right to re-embed.</translation>
    </message>
    <message>
        <source> 段 · </source>
        <translation type="vanished"> chunks · </translation>
    </message>
    <message>
        <source> 字符</source>
        <translation type="vanished"> chars</translation>
    </message>
    <message>
        <source>重新嵌入</source>
        <translation type="vanished">Re-embed</translation>
    </message>
</context>
<context>
    <name>CopyButton</name>
    <message>
        <location filename="../src/ui/components/CopyButton.qml" line="39"/>
        <location filename="../src/ui/components/CopyButton.qml" line="65"/>
        <source>已复制</source>
        <translation>Copied</translation>
    </message>
    <message>
        <location filename="../src/ui/components/CopyButton.qml" line="39"/>
        <location filename="../src/ui/components/CopyButton.qml" line="65"/>
        <source>复制</source>
        <translation>Copy</translation>
    </message>
</context>
<context>
    <name>DataPage</name>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="17"/>
        <source>数据管理页</source>
        <translation>Data Management Page</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="18"/>
        <source>查看数据库体积与各表行数，执行立即清理、清空用量与压缩数据库</source>
        <translation>View database size and table row counts; run retention cleanup, clear usage, and compact the database</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="39"/>
        <source>统计失败：%1</source>
        <translation>Stats failed: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="73"/>
        <source>已按保留天数清理审计与用量记录</source>
        <translation>Expired audit and usage records pruned by retention days</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="77"/>
        <source>已清空全部 Token 用量记录</source>
        <translation>All token usage records cleared</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="81"/>
        <source>数据库已压缩</source>
        <translation>Database compacted</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="94"/>
        <source>数据管理</source>
        <translation>Data Management</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="95"/>
        <source>数据库体积、各表行数与维护操作。删除类操作前建议先在「备份与恢复」生成一份快照。</source>
        <translation>Database size, table row counts and maintenance actions. Take a snapshot in Backup &amp; Restore before deleting anything.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="100"/>
        <source>数据库概览</source>
        <translation>Database Overview</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="102"/>
        <source>统计加载中…</source>
        <translation>Loading stats…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="113"/>
        <source>数据库文件：%1</source>
        <translation>Database file: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="121"/>
        <source>WAL 文件：%1</source>
        <translation>WAL file: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="131"/>
        <source>会话</source>
        <translation>Sessions</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="132"/>
        <source>消息</source>
        <translation>Messages</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="133"/>
        <source>长期记忆</source>
        <translation>Long-term memory</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="134"/>
        <source>知识库文档</source>
        <translation>Knowledge documents</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="135"/>
        <source>知识库分块</source>
        <translation>Knowledge chunks</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="136"/>
        <source>过程轨迹</source>
        <translation>Trace nodes</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="137"/>
        <source>文件检查点</source>
        <translation>File checkpoints</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="138"/>
        <source>活动记录</source>
        <translation>Activity log</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="139"/>
        <source>Token 用量</source>
        <translation>Token usage</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="140"/>
        <source>审计记录</source>
        <translation>Audit log</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="145"/>
        <source>：%1</source>
        <translation>: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="158"/>
        <source>统计中…</source>
        <translation>Collecting stats…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="158"/>
        <source>刷新统计</source>
        <translation>Refresh stats</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="178"/>
        <source>维护操作</source>
        <translation>Maintenance</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="179"/>
        <source>这些操作会删除数据或独占数据库，点击后都需要二次确认；建议先备份。</source>
        <translation>These actions delete data or lock the database and all ask for confirmation; back up first.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="187"/>
        <source>按保留天数清理</source>
        <translation>Retention cleanup</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="188"/>
        <source>按「记忆管理」页设置的保留天数，立即清理过期的审计与 Token 用量记录。</source>
        <translation>Prune audit and token usage rows older than the retention days configured on the Memory page.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="192"/>
        <source>立即清理</source>
        <translation>Clean up now</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="202"/>
        <source>清空用量</source>
        <translation>Clear usage</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="203"/>
        <source>删除全部 Token 用量明细；用量统计与配额计数随之归零，无法撤销。</source>
        <translation>Deletes all token usage details; usage statistics and quota counters reset to zero. Cannot be undone.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="207"/>
        <source>清空</source>
        <translation>Clear</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="217"/>
        <source>压缩数据库</source>
        <translation>Compact database</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="218"/>
        <source>执行 VACUUM 回收已删除数据占用的空间。期间界面可能短暂卡顿；数据库被占用时会失败。</source>
        <translation>Runs VACUUM to reclaim space taken by deleted data. The UI may briefly freeze; fails while the database is in use.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="222"/>
        <source>压缩</source>
        <translation>Compact</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="235"/>
        <source>备份入口</source>
        <translation>Backup entry</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="237"/>
        <source>最近备份：%1（%2，%3）</source>
        <translation>Latest backup: %1 (%2, %3)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="241"/>
        <source>备份目录中还没有快照；「备份与恢复」页可生成与恢复。</source>
        <translation>No snapshot in the backups directory yet; create or restore one on the Backup &amp; Restore page.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="249"/>
        <source>立即备份</source>
        <translation>Back up now</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="253"/>
        <source>备份已生成</source>
        <translation>Backup created</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="262"/>
        <source>打开备份目录</source>
        <translation>Open backups folder</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="287"/>
        <source>立即清理过期记录？</source>
        <translation>Prune expired records now?</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="289"/>
        <source>清空全部 Token 用量？</source>
        <translation>Clear all token usage?</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="290"/>
        <source>压缩数据库？</source>
        <translation>Compact the database?</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="299"/>
        <source>将删除超过保留天数的审计与用量记录，无法撤销。</source>
        <translation>Audit and usage rows older than the retention days will be deleted. Cannot be undone.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="301"/>
        <source>全部 Token 用量明细将被删除，用量统计与配额计数归零，无法撤销。</source>
        <translation>All token usage details will be deleted and usage statistics and quota counters reset to zero. Cannot be undone.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="302"/>
        <source>执行期间界面可能短暂卡顿；若数据库正被占用会失败，可稍后重试。</source>
        <translation>The UI may briefly freeze during the run; it fails while the database is in use — try again later.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="314"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/DataPage.qml" line="322"/>
        <source>确认执行</source>
        <translation>Confirm</translation>
    </message>
</context>
<context>
    <name>DiffView</name>
    <message>
        <location filename="../src/ui/components/DiffView.qml" line="18"/>
        <source>此记录没有保存内容差异。</source>
        <translation>No content diff was saved for this record.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/DiffView.qml" line="592"/>
        <source>拖动调节两栏宽度 · 双击按内容自适应</source>
        <translation>Drag to resize the two columns · double-click to fit the content</translation>
    </message>
    <message>
        <location filename="../src/ui/components/DiffView.qml" line="595"/>
        <source>改动前后两栏分隔条</source>
        <translation>Divider between the before and after columns</translation>
    </message>
    <message>
        <location filename="../src/ui/components/DiffView.qml" line="596"/>
        <source>拖动调节原内容与改动后内容的宽度，双击按内容自适应</source>
        <translation>Drag to resize the original and changed content, double-click to fit the content</translation>
    </message>
    <message>
        <location filename="../src/ui/components/DiffView.qml" line="616"/>
        <source>差异过大，仅显示截断后的部分；撤销仍使用完整的改动前内容。</source>
        <translation>Diff too large; only a truncated portion is shown. Undo still uses the full pre-change content.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/DiffView.qml" line="625"/>
        <source>差异内容</source>
        <translation>Diff content</translation>
    </message>
</context>
<context>
    <name>DonutChart</name>
    <message>
        <location filename="../src/ui/components/charts/DonutChart.qml" line="36"/>
        <source>总量</source>
        <translation>Total</translation>
    </message>
    <message>
        <location filename="../src/ui/components/charts/DonutChart.qml" line="101"/>
        <source>暂无数据</source>
        <translation>No data</translation>
    </message>
    <message>
        <location filename="../src/ui/components/charts/DonutChart.qml" line="104"/>
        <location filename="../src/ui/components/charts/DonutChart.qml" line="209"/>
        <source>%1: %2（%3）</source>
        <translation>%1: %2 (%3)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/charts/DonutChart.qml" line="106"/>
        <source>共 %1 个模型，总计 %2</source>
        <translation>%1 model(s), total %2</translation>
    </message>
    <message>
        <location filename="../src/ui/components/charts/DonutChart.qml" line="148"/>
        <source>该范围内暂无数据</source>
        <translation>No data in this range</translation>
    </message>
    <message>
        <location filename="../src/ui/components/charts/DonutChart.qml" line="383"/>
        <source>模型 Token 分布环形图</source>
        <translation>Model token distribution donut chart</translation>
    </message>
</context>
<context>
    <name>EditBuffer</name>
    <message>
        <location filename="../src/viewmodels/PreferenceRegistry.cpp" line="140"/>
        <source>值必须是布尔值</source>
        <translation>Value must be a boolean</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PreferenceRegistry.cpp" line="148"/>
        <source>值必须是整数</source>
        <translation>Value must be an integer</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PreferenceRegistry.cpp" line="153"/>
        <source>值必须在 %1-%2 之间</source>
        <translation>Value must be between %1 and %2</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PreferenceRegistry.cpp" line="163"/>
        <source>值必须是以下之一: </source>
        <translation>Value must be one of: </translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PreferenceRegistry.cpp" line="170"/>
        <source>此字段为必填</source>
        <translation>This field is required</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PreferenceRegistry.cpp" line="175"/>
        <source>长度不能超过 %1 个字符</source>
        <translation>Must not exceed %1 characters</translation>
    </message>
</context>
<context>
    <name>ErrorCard</name>
    <message>
        <location filename="../src/ui/components/ErrorCard.qml" line="47"/>
        <source>重试本轮</source>
        <translation>Retry this turn</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ErrorCard.qml" line="52"/>
        <source>模型设置</source>
        <translation>Model settings</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ErrorCard.qml" line="60"/>
        <source>错误</source>
        <translation>Error</translation>
    </message>
</context>
<context>
    <name>FileChangeDetailDialog</name>
    <message>
        <location filename="../src/ui/components/FileChangeDetailDialog.qml" line="163"/>
        <source>已撤销</source>
        <translation>Undone</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangeDetailDialog.qml" line="164"/>
        <source>已确认</source>
        <translation>Accepted</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangeDetailDialog.qml" line="165"/>
        <source>仅记录路径，内容未保存</source>
        <translation>Path only; content was not saved</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangeDetailDialog.qml" line="166"/>
        <source>已修改</source>
        <translation>Modified</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangeDetailDialog.qml" line="166"/>
        <source>新建文件</source>
        <translation>Created</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangeDetailDialog.qml" line="178"/>
        <source>原文件超过单文件上限，仅记录了路径，无法展示内容差异；请用 git 查看或恢复。</source>
        <translation>The original file exceeds the per-file limit, so only its path was recorded and no diff can be shown. Use git to inspect or restore it.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangeDetailDialog.qml" line="180"/>
        <source>该新建文件没有可读取的内容：它可能已被删除或改名。</source>
        <translation>The new file has no readable content; it may have been deleted or renamed.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangeDetailDialog.qml" line="181"/>
        <source>此记录没有保存内容差异。</source>
        <translation>No content diff was saved for this record.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangeDetailDialog.qml" line="275"/>
        <source>上一个文件</source>
        <translation>Previous file</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangeDetailDialog.qml" line="289"/>
        <source>下一个文件</source>
        <translation>Next file</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangeDetailDialog.qml" line="303"/>
        <source>统一</source>
        <translation>Unified</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangeDetailDialog.qml" line="314"/>
        <source>并排</source>
        <translation>Side by side</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangeDetailDialog.qml" line="330"/>
        <source>确认</source>
        <translation>Accept</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangeDetailDialog.qml" line="336"/>
        <source>确认该文件在本会话的改动</source>
        <translation>Accept this file&apos;s changes in this session</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangeDetailDialog.qml" line="342"/>
        <source>撤销</source>
        <translation>Undo</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangeDetailDialog.qml" line="347"/>
        <source>撤销该文件在本会话的全部改动</source>
        <translation>Undo all changes to this file in this session</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangeDetailDialog.qml" line="47"/>
        <source>在上方选择一个文件查看差异</source>
        <translation>Select a file above to see the diff</translation>
    </message>
    <message>
        <source>关闭文件改动详情</source>
        <translation type="vanished">Close file change details</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangeDetailDialog.qml" line="354"/>
        <source>该记录未保存差异，以下按当前文件内容重建（文件未再改动时即为当时写入的内容）。</source>
        <translation>No diff was stored for this record; the comparison below is rebuilt from the current file content (accurate if the file has not changed since).</translation>
    </message>
    <message>
        <source>共 %1 次改动</source>
        <translation type="vanished">%1 changes</translation>
    </message>
    <message>
        <source>本次会话共 %1 次改动</source>
        <translation type="vanished">%1 changes in this session</translation>
    </message>
    <message>
        <source>本次改动</source>
        <translation type="vanished">This change</translation>
    </message>
    <message>
        <source>工作区</source>
        <translation type="vanished">Working tree</translation>
    </message>
    <message>
        <source>Git 历史</source>
        <translation type="vanished">Git history</translation>
    </message>
    <message>
        <source>不在 Git 仓库中，仅显示本次改动</source>
        <translation type="vanished">Not in a Git repository; showing this change only</translation>
    </message>
    <message>
        <source>本会话文件 · %1</source>
        <translation type="vanished">Session files · %1</translation>
    </message>
    <message>
        <source>正在读取工作区差异…</source>
        <translation type="vanished">Reading working-tree diff…</translation>
    </message>
    <message>
        <source>没有未提交的改动（未跟踪文件不显示内容）。</source>
        <translation type="vanished">No uncommitted changes (untracked files are not shown).</translation>
    </message>
    <message>
        <source>查看该提交的差异</source>
        <translation type="vanished">View this commit&apos;s diff</translation>
    </message>
    <message>
        <source>正在读取 Git 历史…</source>
        <translation type="vanished">Reading Git history…</translation>
    </message>
    <message>
        <source>该文件没有 Git 历史。</source>
        <translation type="vanished">This file has no Git history.</translation>
    </message>
    <message>
        <source>正在读取提交差异…</source>
        <translation type="vanished">Reading commit diff…</translation>
    </message>
    <message>
        <source>点击左侧提交查看差异。</source>
        <translation type="vanished">Select a commit on the left to view its diff.</translation>
    </message>
    <message>
        <source>关闭</source>
        <translation type="vanished">Close</translation>
    </message>
</context>
<context>
    <name>FileChangesPanel</name>
    <message>
        <source>已撤销</source>
        <translation type="vanished">Undone</translation>
    </message>
    <message>
        <source>仅记录路径，请用 git 恢复</source>
        <translation type="vanished">Path only — restore with git</translation>
    </message>
    <message>
        <source>已全部撤销</source>
        <translation type="vanished">All undone</translation>
    </message>
    <message>
        <source>可恢复改动前内容</source>
        <translation type="vanished">Restore the content from before this change</translation>
    </message>
    <message>
        <source>将删除本次新建的文件</source>
        <translation type="vanished">Delete the file created by this change</translation>
    </message>
    <message>
        <source>%1 · 共 %2 次改动</source>
        <translation type="vanished">%1 · %2 changes</translation>
    </message>
    <message>
        <source>%1 · 剩余 %2 次改动</source>
        <translation type="vanished">%1 · %2 changes remaining</translation>
    </message>
    <message>
        <source>可回退本次会话的全部改动</source>
        <translation type="vanished">Reverts all changes from this session</translation>
    </message>
    <message>
        <source>将删除本会话新建的文件</source>
        <translation type="vanished">Deletes the file created in this session</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="358"/>
        <source>撤销失败：文件或检查点已不可用。</source>
        <translation>Undo failed: the file or checkpoint is no longer available.</translation>
    </message>
    <message>
        <source>文件改动 · %1</source>
        <translation type="vanished">File changes · %1</translation>
    </message>
    <message>
        <source>展开或折叠文件改动列表</source>
        <translation type="vanished">Expand or collapse the file changes list</translation>
    </message>
    <message>
        <source>查看文件改动详情</source>
        <translation type="vanished">View file change details</translation>
    </message>
    <message>
        <source>不可撤销</source>
        <translation type="vanished">Cannot undo</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="291"/>
        <source>原文件过大，仅记录了路径，请用 git 恢复</source>
        <translation>Original file too large; only its path was recorded. Restore it with git.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="296"/>
        <source>确认该文件在本会话的改动</source>
        <translation>Accept this file&apos;s changes in this session</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="315"/>
        <source>确认失败：检查点已撤销或已确认。</source>
        <translation>Could not accept: the checkpoint was already undone or accepted.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="323"/>
        <source>没有待确认的改动。</source>
        <translation>Nothing left to accept.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="335"/>
        <source>待确认的文件都只记录了路径，无法撤销。</source>
        <translation>The pending files were recorded by path only, so they cannot be reverted.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="336"/>
        <source>没有待撤销的改动。</source>
        <translation>Nothing is waiting to be reverted.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="348"/>
        <source>已撤销 %1 个文件，%2 个未能还原：%3</source>
        <translation>Reverted %1 file(s); %2 could not be restored: %3</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="351"/>
        <source>没有可撤销的改动。</source>
        <translation>There was nothing to revert.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="613"/>
        <source>撤销</source>
        <translation>Undo</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="644"/>
        <source>撤销全部待确认的改动？</source>
        <translation>Revert all pending changes?</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="653"/>
        <source>将把下列 %1 个文件恢复为本次会话首次改动前的内容；新建的文件会被删除。此操作无法撤销。</source>
        <translation>The following %1 file(s) will be restored to their state before this session first touched them; files the agent created will be deleted. This cannot be undone.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="693"/>
        <source>其中 %1 个文件只记录了路径（原文件过大），无法撤销，将被跳过。</source>
        <translation>%1 of them were recorded by path only (the original was too large) and will be skipped.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="713"/>
        <source>全部撤销</source>
        <translation>Revert All</translation>
    </message>
    <message>
        <source>本会话对该文件的改动已撤销</source>
        <translation type="vanished">This file&apos;s changes in this session were undone</translation>
    </message>
    <message>
        <source>撤销该文件在本会话的全部改动</source>
        <translation type="vanished">Undo all changes to this file in this session</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="391"/>
        <source>展开或折叠审阅列表</source>
        <translation>Expand or collapse the review list</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="406"/>
        <source>审阅 · %1</source>
        <translation>Review · %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="441"/>
        <source>折叠全部目录</source>
        <translation>Collapse all folders</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="453"/>
        <source>展开全部目录</source>
        <translation>Expand all folders</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="541"/>
        <source>在左侧选择一个文件查看差异</source>
        <translation>Select a file on the left to see the diff</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="542"/>
        <source>在上方选择一个文件查看差异</source>
        <translation>Select a file above to see the diff</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="589"/>
        <source>将把以下文件恢复为本次会话首次改动前的内容：
%1
此后对该文件的手工修改也会被覆盖。</source>
        <translation>The following file will be restored to its state before this session&apos;s first change:
%1
Manual edits made after that will also be overwritten.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="591"/>
        <source>将删除模型在本会话新建的文件：
%1</source>
        <translation>The file the model created in this session will be deleted:
%1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="594"/>
        <source>该文件在本会话共有 %1 次改动，本次将全部撤销。</source>
        <translation>This file has %1 changes in this session; all of them will be undone.</translation>
    </message>
    <message>
        <source>撤销该文件的本次改动</source>
        <translation type="vanished">Undo this change to the file</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="578"/>
        <source>撤销文件改动？</source>
        <translation>Undo file change?</translation>
    </message>
    <message>
        <source>将把以下文件恢复为最近一次改动前的内容：
%1
此后对该文件的手工修改也会被覆盖。</source>
        <translation type="vanished">The following file will be restored to its state before the most recent change:
%1
Manual edits made after that will also be overwritten.</translation>
    </message>
    <message>
        <source>将把以下文件恢复为改动前内容：
%1
此后对该文件的手工修改也会被覆盖。</source>
        <translation type="vanished">Restore the following file to its content before this change:
%1
Any manual edits made since then will be overwritten.</translation>
    </message>
    <message>
        <source>将删除模型本次新建的文件：
%1</source>
        <translation type="vanished">Delete the file created by the model:
%1</translation>
    </message>
    <message>
        <source>该文件在本会话共有 %1 次改动，撤销会从最近一次开始逐步回退。</source>
        <translation type="vanished">This file has %1 changes in this session; undo steps back one change at a time.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="609"/>
        <location filename="../src/ui/components/FileChangesPanel.qml" line="707"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
</context>
<context>
    <name>GeneralPage</name>
    <message>
        <source>通用设置页</source>
        <translation type="vanished">General settings page</translation>
    </message>
    <message>
        <source>通用设置</source>
        <translation type="vanished">General</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="16"/>
        <source>常规应用页</source>
        <translation>General settings page</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="18"/>
        <source>常规应用</source>
        <translation>General</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="19"/>
        <source>本页修改实时保存，无需手动提交；「恢复默认」在窗口底部。</source>
        <translation>Changes on this page save instantly; &quot;Restore defaults&quot; lives at the bottom of the window</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="33"/>
        <source>启动与通知</source>
        <translation>Startup &amp; Notifications</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="53"/>
        <source>关于 NexusAI</source>
        <translation>About NexusAI</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="78"/>
        <source>应用版本</source>
        <translation>App version</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="89"/>
        <source>Qt 版本</source>
        <translation>Qt version</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="100"/>
        <source>编译器</source>
        <translation>Compiler</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="111"/>
        <source>CPU 架构</source>
        <translation>CPU architecture</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="122"/>
        <source>操作系统</source>
        <translation>Operating system</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="134"/>
        <source>数据目录</source>
        <translation>Data directory</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="153"/>
        <source>打开数据目录</source>
        <translation>Open data directory</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="158"/>
        <source>无法打开数据目录。</source>
        <translation>Cannot open the data directory.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="161"/>
        <source>在文件管理器中打开应用数据目录</source>
        <translation>Open the app data directory in the file manager</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="165"/>
        <source>导出诊断包</source>
        <translation>Export diagnostics</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="167"/>
        <source>选择目录并导出诊断包</source>
        <translation>Choose a folder and export diagnostics</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="171"/>
        <location filename="../src/ui/pages/GeneralPage.qml" line="217"/>
        <source>开源许可</source>
        <translation>Open-source licenses</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="173"/>
        <source>查看开源组件许可</source>
        <translation>View licenses of open-source components</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="193"/>
        <source>选择诊断包保存位置</source>
        <translation>Choose where to save the diagnostics bundle</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="199"/>
        <source>已导出：%1</source>
        <translation>Exported: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="200"/>
        <source>导出失败。</source>
        <translation>Export failed.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="238"/>
        <source>本应用链接这些组件但未修改其源码；完整许可文本随组件分发。</source>
        <translation>This app links these components without modifying their source; full license texts ship with the components.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="249"/>
        <source>关闭</source>
        <translation>Close</translation>
    </message>
    <message>
        <source>偏好</source>
        <translation type="vanished">Preferences</translation>
    </message>
    <message>
        <source>应用行为</source>
        <translation type="vanished">Application Behavior</translation>
    </message>
    <message>
        <source>上下文窗口 (Tokens)</source>
        <translation type="vanished">Context Window (Tokens)</translation>
    </message>
    <message>
        <source>默认值。按模型能力表自动适配时，此项作为未知模型的 fallback。</source>
        <translation type="vanished">Default value. When auto-adapting by model capability, this is the fallback for unknown models.</translation>
    </message>
    <message>
        <source>恢复本页默认</source>
        <translation type="vanished">Reset Page Defaults</translation>
    </message>
    <message>
        <source>恢复通用设置默认值</source>
        <translation type="vanished">Reset General Settings Defaults</translation>
    </message>
    <message>
        <source>安全与隐私</source>
        <translation type="vanished">Security &amp; Privacy</translation>
    </message>
    <message>
        <source>允许读取整个磁盘</source>
        <translation type="vanished">Allow reading the entire disk</translation>
    </message>
    <message>
        <source>关闭时文件读取仅限文档/下载/桌面/应用目录；开启后除系统目录外全部可读，模型读到的内容可能发送给云端模型。</source>
        <translation type="vanished">When off, file reads are limited to Documents/Downloads/Desktop/AppData; when on, everything except system directories is readable, and anything the model reads may be sent to the cloud model.</translation>
    </message>
    <message>
        <source>仅本地模式</source>
        <translation type="vanished">Local-only mode</translation>
    </message>
    <message>
        <source>使用云端模型时拦截知识库搜索、联网搜索与全部 MCP 工具（工具页会显示拦截徽标）；本地模型不受影响。</source>
        <translation type="vanished">With cloud models this blocks knowledge search, web search and all MCP tools (a badge shows on the Tools page); local models are unaffected.</translation>
    </message>
    <message>
        <source>允许 http_fetch 访问本机与内网地址</source>
        <translation type="vanished">Allow http_fetch to access localhost and intranet addresses</translation>
    </message>
    <message>
        <source>关闭时 http_fetch 拒绝本机/内网目标（防 SSRF）；仅在确有访问内网服务的需求时开启。</source>
        <translation type="vanished">When off, http_fetch rejects local/intranet targets (SSRF guard); enable only if intranet access is truly needed.</translation>
    </message>
    <message>
        <source>模型行为</source>
        <translation type="vanished">Model Behavior</translation>
    </message>
    <message>
        <source>上下文窗口 Tokens</source>
        <translation type="vanished">Context Window (Tokens)</translation>
    </message>
    <message>
        <source>单次任务步数预算</source>
        <translation type="vanished">Step budget per task</translation>
    </message>
    <message>
        <source>一次任务里模型最多进行多少轮「思考 → 工具调用」（3–1000）。用完会强制基于已有信息作答；复杂任务可调高，但通常会增加耗时和 token 消耗。</source>
        <translation type="vanished">Maximum &quot;think -&gt; tool call&quot; rounds per task (3–1000). When the budget runs out, the model must answer from what it has gathered. Increase it for difficult tasks, noting this may increase latency and token use.</translation>
    </message>
    <message>
        <source>自定义系统提示词</source>
        <translation type="vanished">Custom System Prompt</translation>
    </message>
    <message>
        <source>日志</source>
        <translation type="vanished">Logging</translation>
    </message>
    <message>
        <source>日志级别</source>
        <translation type="vanished">Log Level</translation>
    </message>
    <message>
        <source>最低记录级别</source>
        <translation type="vanished">Minimum log level</translation>
    </message>
    <message>
        <source>日志目录</source>
        <translation type="vanished">Log directory</translation>
    </message>
    <message>
        <source>当前日志目录</source>
        <translation type="vanished">Current log directory</translation>
    </message>
    <message>
        <source>选择目录</source>
        <translation type="vanished">Choose directory</translation>
    </message>
    <message>
        <source>恢复默认目录</source>
        <translation type="vanished">Restore default directory</translation>
    </message>
    <message>
        <source>打开目录</source>
        <translation type="vanished">Open directory</translation>
    </message>
    <message>
        <source>选择日志保存目录</source>
        <translation type="vanished">Choose log directory</translation>
    </message>
    <message>
        <source>单个日志文件上限（MB）</source>
        <translation type="vanished">Maximum size per log file (MB)</translation>
    </message>
    <message>
        <source>单个日志文件大小上限，兆字节</source>
        <translation type="vanished">Maximum log file size in megabytes</translation>
    </message>
    <message>
        <source>保留日志文件数</source>
        <translation type="vanished">Number of log files to keep</translation>
    </message>
    <message>
        <source>达到单文件大小上限后自动轮转；只保留最新的指定数量文件。日志可能包含本地路径和错误上下文，请选择安全的保存位置。</source>
        <translation type="vanished">Files rotate when they reach the size limit; only the newest configured number are kept. Logs may contain local paths and error context, so choose a secure location.</translation>
    </message>
    <message>
        <source>单次任务超时（分钟）</source>
        <translation type="vanished">Task timeout (minutes)</translation>
    </message>
    <message>
        <source>单次任务超时，分钟</source>
        <translation type="vanished">Task timeout in minutes</translation>
    </message>
    <message>
        <source>任务运行超过此时长会停止并基于已收集的信息作答。较难或需要多次网络调用的任务可以适当调高。</source>
        <translation type="vanished">Tasks stop after this duration and answer using the information collected so far. Increase it for difficult tasks or tasks requiring multiple network calls.</translation>
    </message>
    <message>
        <source>知识库嵌入</source>
        <translation type="vanished">Knowledge Embedding</translation>
    </message>
    <message>
        <source>嵌入服务提供方</source>
        <translation type="vanished">Embedding provider</translation>
    </message>
    <message>
        <source>本地 Ollama</source>
        <translation type="vanished">Local Ollama</translation>
    </message>
    <message>
        <source>OpenAI 兼容服务</source>
        <translation type="vanished">OpenAI-compatible service</translation>
    </message>
    <message>
        <source>网络代理</source>
        <translation type="vanished">HTTP Proxy</translation>
    </message>
    <message>
        <source>代理设置对模型服务、向量服务、联网搜索与 MCP HTTP 连接生效；本机地址始终直连。</source>
        <translation type="vanished">Proxy settings apply to model, embedding, web search and MCP HTTP connections; local addresses always connect directly.</translation>
    </message>
    <message>
        <source>代理模式</source>
        <translation type="vanished">Proxy mode</translation>
    </message>
    <message>
        <source>跟随系统</source>
        <translation type="vanished">Follow system</translation>
    </message>
    <message>
        <source>手动代理</source>
        <translation type="vanished">Manual proxy</translation>
    </message>
    <message>
        <source>不使用代理</source>
        <translation type="vanished">No proxy</translation>
    </message>
    <message>
        <source>proxy.example.com（不含 http://）</source>
        <translation type="vanished">proxy.example.com (without http://)</translation>
    </message>
    <message>
        <source>代理服务器地址</source>
        <translation type="vanished">Proxy server address</translation>
    </message>
    <message>
        <source>代理端口</source>
        <translation type="vanished">Proxy port</translation>
    </message>
    <message>
        <source>用户名（可选）</source>
        <translation type="vanished">Username (optional)</translation>
    </message>
    <message>
        <source>代理认证用户名</source>
        <translation type="vanished">Proxy authentication username</translation>
    </message>
    <message>
        <source>代理用户名</source>
        <translation type="vanished">Proxy username</translation>
    </message>
    <message>
        <source>密码（可选）</source>
        <translation type="vanished">Password (optional)</translation>
    </message>
    <message>
        <source>代理密码</source>
        <translation type="vanished">Proxy password</translation>
    </message>
    <message>
        <source>清除密码</source>
        <translation type="vanished">Clear password</translation>
    </message>
    <message>
        <source>清除代理密码</source>
        <translation type="vanished">Clear proxy password</translation>
    </message>
    <message>
        <source>测试代理</source>
        <translation type="vanished">Test proxy</translation>
    </message>
    <message>
        <source>测试代理连接</source>
        <translation type="vanished">Test proxy connection</translation>
    </message>
    <message>
        <source>嵌入模型</source>
        <translation type="vanished">Embedding Model</translation>
    </message>
    <message>
        <source>知识库嵌入模型</source>
        <translation type="vanished">Knowledge Base Embedding Model</translation>
    </message>
    <message>
        <source>嵌入服务地址</source>
        <translation type="vanished">Embedding Endpoint</translation>
    </message>
    <message>
        <source>https://api.openai.com 或 https://api.siliconflow.cn，可带或不带 /v1</source>
        <translation type="vanished">https://api.openai.com or https://api.siliconflow.cn, with or without /v1</translation>
    </message>
    <message>
        <source>http://localhost:11434</source>
        <translation type="vanished">http://localhost:11434</translation>
    </message>
    <message>
        <source>API Key</source>
        <translation type="vanished">API Key</translation>
    </message>
    <message>
        <source>已配置（输入新值可覆盖，留空不修改）</source>
        <translation type="vanished">Configured (enter a new value to overwrite, leave blank to keep)</translation>
    </message>
    <message>
        <source>未配置</source>
        <translation type="vanished">Not configured</translation>
    </message>
    <message>
        <source>向量模型 API Key</source>
        <translation type="vanished">Embedding API key</translation>
    </message>
    <message>
        <source>清除密钥</source>
        <translation type="vanished">Clear key</translation>
    </message>
    <message>
        <source>清除向量模型 API Key</source>
        <translation type="vanished">Clear embedding API key</translation>
    </message>
    <message>
        <source>文档内容将发送到所配置的外部服务生成向量，请勿导入敏感资料。</source>
        <translation type="vanished">Document content will be sent to the configured external service to generate embeddings. Do not import sensitive material.</translation>
    </message>
    <message>
        <source>纯本地模式已开启：外部向量请求将被拦截，并自动回退本地 Ollama。</source>
        <translation type="vanished">Local-only mode is on: external embedding requests are blocked and fall back to local Ollama.</translation>
    </message>
    <message>
        <source>恢复通用设置默认值？</source>
        <translation type="vanished">Reset general settings to defaults?</translation>
    </message>
    <message>
        <source>将重置上下文窗口、系统提示词、日志级别、嵌入配置，</source>
        <translation type="vanished">This resets the context window, system prompt, log level and embedding settings,</translation>
    </message>
    <message>
        <source>并关闭「允许读取整个磁盘」「仅本地模式」「允许内网访问」三个安全开关。确定继续吗？</source>
        <translation type="vanished"> and turns OFF the three security switches: full-disk read, local-only mode and intranet fetch. Continue?</translation>
    </message>
    <message>
        <source>取消</source>
        <translation type="vanished">Cancel</translation>
    </message>
    <message>
        <source>恢复默认</source>
        <translation type="vanished">Reset Defaults</translation>
    </message>
    <message>
        <source>智能体</source>
        <translation type="vanished">Agent</translation>
    </message>
    <message>
        <source>自定义系统提示词：留空使用内置提示词；修改即时生效，内容随请求发送给当前模型服务</source>
        <translation type="vanished">Custom system prompt: leave empty to use the built-in prompt; changes take effect immediately and are sent to the current model service</translation>
    </message>
    <message>
        <source>留空 = 内置提示词</source>
        <translation type="vanished">Empty = built-in prompt</translation>
    </message>
    <message>
        <source>%1 字符</source>
        <translation type="vanished">%1 chars</translation>
    </message>
    <message>
        <source>恢复默认提示词</source>
        <translation type="vanished">Restore Default Prompt</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/GeneralPage.qml" line="67"/>
        <source>本地智能体桌面应用 · 多模型提供商 · 工具调用 · 本地知识库</source>
        <translation>Local agent desktop app · Multiple model providers · Tool calling · Local knowledge base</translation>
    </message>
</context>
<context>
    <name>GitActionDialog</name>
    <message>
        <location filename="../src/ui/components/GitActionDialog.qml" line="16"/>
        <location filename="../src/ui/components/GitActionDialog.qml" line="34"/>
        <source>确认</source>
        <translation>Confirm</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitActionDialog.qml" line="128"/>
        <source>必须填写说明。</source>
        <translation>A message is required.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitActionDialog.qml" line="157"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
</context>
<context>
    <name>GitBlameDialog</name>
    <message>
        <location filename="../src/ui/components/GitBlameDialog.qml" line="27"/>
        <source>暂无归属</source>
        <translation>no attribution yet</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBlameDialog.qml" line="49"/>
        <source>未提交</source>
        <translation>uncommitted</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBlameDialog.qml" line="110"/>
        <source>逐行归属 · %1</source>
        <translation>Line attribution · %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBlameDialog.qml" line="122"/>
        <source>重新读取逐行归属</source>
        <translation>Reload line attribution</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBlameDialog.qml" line="129"/>
        <source>关闭逐行归属</source>
        <translation>Close line attribution</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBlameDialog.qml" line="138"/>
        <source>%1 行 · 作者：%2</source>
        <translation>%1 lines · authors: %2</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBlameDialog.qml" line="150"/>
        <source>正在读取逐行归属…</source>
        <translation>Reading line attribution…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBlameDialog.qml" line="199"/>
        <source>查看该行的归属提交</source>
        <translation>Open the commit that owns this line</translation>
    </message>
</context>
<context>
    <name>GitBranchDialog</name>
    <message>
        <location filename="../src/ui/components/GitBranchDialog.qml" line="44"/>
        <source>无上游</source>
        <translation>No upstream</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBranchDialog.qml" line="46"/>
        <source>上游已删除</source>
        <translation>Upstream gone</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBranchDialog.qml" line="48"/>
        <source>↑%1 ↓%2</source>
        <translation>↑%1 ↓%2</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBranchDialog.qml" line="50"/>
        <source>↑%1</source>
        <translation>↑%1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBranchDialog.qml" line="52"/>
        <source>↓%1</source>
        <translation>↓%1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBranchDialog.qml" line="53"/>
        <source>已同步</source>
        <translation>In sync</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBranchDialog.qml" line="96"/>
        <source>分支与远端</source>
        <translation>Branches and remotes</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBranchDialog.qml" line="107"/>
        <source>重新读取分支信息</source>
        <translation>Reload branch information</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBranchDialog.qml" line="114"/>
        <source>关闭分支视图</source>
        <translation>Close branch view</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBranchDialog.qml" line="122"/>
        <source>仅查看：本面板不切换分支、不创建分支，也不访问远端。</source>
        <translation>Read-only: this panel does not switch or create branches, and never contacts a remote.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBranchDialog.qml" line="134"/>
        <source>正在读取分支…</source>
        <translation>Reading branches…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBranchDialog.qml" line="177"/>
        <source>本地分支 · %1</source>
        <translation>Local branches · %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBranchDialog.qml" line="217"/>
        <source>当前</source>
        <translation>Current</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBranchDialog.qml" line="248"/>
        <source>远端 · %1</source>
        <translation>Remotes · %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBranchDialog.qml" line="287"/>
        <source>跟踪分支 %1 个</source>
        <translation>%1 tracking branches</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBranchDialog.qml" line="299"/>
        <source>标签 · %1</source>
        <translation>Tags · %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBranchDialog.qml" line="359"/>
        <source>附注</source>
        <translation>annotated</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitBranchDialog.qml" line="359"/>
        <source>轻量</source>
        <translation>lightweight</translation>
    </message>
</context>
<context>
    <name>GitCommitDialog</name>
    <message>
        <location filename="../src/ui/components/GitCommitDialog.qml" line="89"/>
        <source>缺少提交标识，无法读取该提交。</source>
        <translation>Missing commit identifier; cannot load this commit.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCommitDialog.qml" line="169"/>
        <source>统一</source>
        <translation>Unified</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCommitDialog.qml" line="179"/>
        <source>并排</source>
        <translation>Side by side</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCommitDialog.qml" line="190"/>
        <source>关闭提交详情</source>
        <translation>Close commit details</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCommitDialog.qml" line="220"/>
        <source>提交说明</source>
        <translation>Commit message</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCommitDialog.qml" line="237"/>
        <source>改动文件 · %1</source>
        <translation>Changed files · %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCommitDialog.qml" line="247"/>
        <source>正在读取提交文件…</source>
        <translation>Reading commit files…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCommitDialog.qml" line="267"/>
        <source>该提交没有文件改动。</source>
        <translation>This commit has no file changes.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCommitDialog.qml" line="330"/>
        <source>正在读取提交差异…</source>
        <translation>Reading commit diff…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCommitDialog.qml" line="358"/>
        <source>该文件在此提交中没有可显示的差异。</source>
        <translation>This file has no displayable diff in this commit.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCommitDialog.qml" line="372"/>
        <source>关闭</source>
        <translation>Close</translation>
    </message>
</context>
<context>
    <name>GitCompareDialog</name>
    <message>
        <location filename="../src/ui/components/GitCompareDialog.qml" line="154"/>
        <source>对比</source>
        <translation>Compare</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCompareDialog.qml" line="192"/>
        <source>交换方向</source>
        <translation>Swap direction</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCompareDialog.qml" line="201"/>
        <source>重新读取对比结果</source>
        <translation>Reload the comparison</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCompareDialog.qml" line="208"/>
        <source>关闭对比视图</source>
        <translation>Close comparison</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCompareDialog.qml" line="221"/>
        <source>正在读取差异文件…</source>
        <translation>Reading differing files…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCompareDialog.qml" line="222"/>
        <source>差异文件 · %1</source>
        <translation>Differing files · %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCompareDialog.qml" line="230"/>
        <source>统一</source>
        <translation>Unified</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCompareDialog.qml" line="240"/>
        <source>并排</source>
        <translation>Side by side</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCompareDialog.qml" line="278"/>
        <source>A 到 B 之间变化的文件</source>
        <translation>Files changed from A to B</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCompareDialog.qml" line="301"/>
        <source>两个提交之间没有文件差异。</source>
        <translation>These two commits have no file differences.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCompareDialog.qml" line="327"/>
        <source>正在读取差异…</source>
        <translation>Reading the diff…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCompareDialog.qml" line="353"/>
        <source>该文件在这两个提交之间没有差异。</source>
        <translation>This file is identical between the two commits.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitCompareDialog.qml" line="354"/>
        <source>选择一个文件查看差异。</source>
        <translation>Select a file to see its diff.</translation>
    </message>
</context>
<context>
    <name>GitFileHistoryDialog</name>
    <message>
        <location filename="../src/ui/components/GitFileHistoryDialog.qml" line="78"/>
        <source>文件历史 · %1</source>
        <translation>File history · %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitFileHistoryDialog.qml" line="90"/>
        <source>重新读取该文件历史</source>
        <translation>Reload this file&apos;s history</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitFileHistoryDialog.qml" line="97"/>
        <source>关闭文件历史</source>
        <translation>Close file history</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitFileHistoryDialog.qml" line="106"/>
        <source>正在读取文件历史…</source>
        <translation>Reading file history…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitFileHistoryDialog.qml" line="130"/>
        <source>该文件还没有提交记录（可能尚未 git add）。</source>
        <translation>This file has no commits yet (it may not be tracked by git).</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitFileHistoryDialog.qml" line="179"/>
        <source>查看该提交</source>
        <translation>View this commit</translation>
    </message>
</context>
<context>
    <name>GitGraphDialog</name>
    <message>
        <location filename="../src/ui/components/GitGraphDialog.qml" line="94"/>
        <source>提交图</source>
        <translation>Commit graph</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitGraphDialog.qml" line="104"/>
        <location filename="../src/ui/components/GitGraphDialog.qml" line="148"/>
        <source>全部分支</source>
        <translation>All branches</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitGraphDialog.qml" line="104"/>
        <location filename="../src/ui/components/GitGraphDialog.qml" line="149"/>
        <source>仅当前分支</source>
        <translation>Current branch only</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitGraphDialog.qml" line="108"/>
        <source>切换图的分支范围</source>
        <translation>Switch the graph&apos;s branch scope</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitGraphDialog.qml" line="119"/>
        <source>重新读取提交图</source>
        <translation>Reload the commit graph</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitGraphDialog.qml" line="126"/>
        <source>关闭提交图</source>
        <translation>Close the commit graph</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitGraphDialog.qml" line="134"/>
        <source>图形来自 git 自身的 --graph，只读本地引用，不会访问远端。</source>
        <translation>The picture is git&apos;s own --graph over local refs; it never contacts a remote.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitGraphDialog.qml" line="146"/>
        <source>%1 个提交 · 范围：%2</source>
        <translation>%1 commits · scope: %2</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitGraphDialog.qml" line="160"/>
        <source>正在读取提交图…</source>
        <translation>Reading the commit graph…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitGraphDialog.qml" line="210"/>
        <source>查看该提交</source>
        <translation>View this commit</translation>
    </message>
</context>
<context>
    <name>GitPanel</name>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="65"/>
        <source>上游已删除</source>
        <translation>Upstream gone</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="67"/>
        <source>↑%1 ↓%2</source>
        <translation>↑%1 ↓%2</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="69"/>
        <source>↑%1</source>
        <translation>↑%1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="71"/>
        <source>↓%1</source>
        <translation>↓%1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="72"/>
        <source>已同步</source>
        <translation>In sync</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="109"/>
        <location filename="../src/ui/components/GitPanel.qml" line="946"/>
        <source>正在识别 Git 仓库…</source>
        <translation>Detecting Git repository…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="110"/>
        <source>不在 Git 仓库</source>
        <translation>Not in a Git repository</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="112"/>
        <source>分离 HEAD @ %1</source>
        <translation>Detached HEAD @ %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="113"/>
        <source>(无分支)</source>
        <translation>(no branch)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="420"/>
        <source>改写最后一次提交？</source>
        <translation>Rewrite the last commit?</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="422"/>
        <source>git commit --amend</source>
        <translation>git commit --amend</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="423"/>
        <source>当前 HEAD：%1 %2</source>
        <translation>Current HEAD: %1 %2</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="424"/>
        <source>同时把已暂存的 %1 个文件并入该提交。</source>
        <translation>The %1 staged files are folded into that commit too.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="425"/>
        <source>已推送到远端的提交会被拒绝；旧提交仍可从 reflog 找回。</source>
        <translation>Commits already pushed to a remote are refused; the old commit stays recoverable from the reflog.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="427"/>
        <source>改写提交</source>
        <translation>Rewrite commit</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="430"/>
        <source>新的提交说明（必填）</source>
        <translation>New commit message (required)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="438"/>
        <source>撤销最后一次提交？</source>
        <translation>Undo the last commit?</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="440"/>
        <source>git reset --soft HEAD^</source>
        <translation>git reset --soft HEAD^</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="441"/>
        <source>将撤销：%1 %2</source>
        <translation>Will undo: %1 %2</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="442"/>
        <source>改动全部退回暂存区，不会丢失。</source>
        <translation>Every change goes back to the staging area; nothing is lost.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="443"/>
        <source>已推送、合并提交或首次提交会被拒绝。</source>
        <translation>Pushed commits, merge commits and the first commit are refused.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="445"/>
        <source>撤销提交</source>
        <translation>Undo commit</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="452"/>
        <source>暂存当前改动（stash）？</source>
        <translation>Stash the current changes?</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="454"/>
        <source>git stash push</source>
        <translation>git stash push</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="455"/>
        <source>只收起已跟踪的改动，未跟踪文件保持原地。</source>
        <translation>Only tracked changes are set aside; untracked files stay in place.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="456"/>
        <source>取出前这些改动不在工作区，其他工具看不到它们。</source>
        <translation>Until you pop them these changes are not in the worktree, so other tools cannot see them.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="458"/>
        <source>暂存改动</source>
        <translation>Stash changes</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="460"/>
        <source>备注（可选）</source>
        <translation>Note (optional)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="494"/>
        <source>该文件未被 git 跟踪，git 里没有它的副本。</source>
        <translation>Git does not track this file, so git holds no copy of it.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="495"/>
        <source>会先复制到备份目录，再删除原文件。</source>
        <translation>It is copied to the backup folder before the original is deleted.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="496"/>
        <source>备份位置：%1</source>
        <translation>Backup location: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="498"/>
        <source>git restore -- %1</source>
        <translation>git restore -- %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="499"/>
        <source>工作区内容回到暂存区版本，已暂存的改动保留。</source>
        <translation>The worktree returns to the staged version; staged changes are kept.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="500"/>
        <source>该操作无法通过 git 撤销。</source>
        <translation>Git cannot undo this action.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="504"/>
        <source>删除未跟踪文件？</source>
        <translation>Delete the untracked file?</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="504"/>
        <source>丢弃该文件的改动？</source>
        <translation>Discard this file&apos;s changes?</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="506"/>
        <source>备份并删除</source>
        <translation>Back up and delete</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="506"/>
        <source>丢弃改动</source>
        <translation>Discard changes</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="217"/>
        <location filename="../src/ui/components/GitPanel.qml" line="532"/>
        <source>当前没有可用的 Git 项目。</source>
        <translation>No Git project is currently available.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="108"/>
        <source>未关联项目</source>
        <translation>No project</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="239"/>
        <source>提交说明已被修改，未覆盖生成结果。</source>
        <translation>The message was edited meanwhile, so the generated text was not applied.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="243"/>
        <source>已生成提交说明，可继续编辑后再提交</source>
        <translation>Commit message drafted - edit it before committing</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="312"/>
        <source>已选为对比基准 %1，再点另一行开始对比</source>
        <translation>%1 set as comparison base; pick another commit to compare</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="318"/>
        <source>已取消对比基准</source>
        <translation>Comparison base cleared</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="540"/>
        <source>取出这条暂存？</source>
        <translation>Pop this stash?</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="542"/>
        <source>git stash pop stash@{%1}</source>
        <translation>git stash pop stash@{%1}</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="543"/>
        <location filename="../src/ui/components/GitPanel.qml" line="1228"/>
        <source>(无备注)</source>
        <translation>(no note)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="544"/>
        <source>改动会立即进入工作区；与工作区现有改动冲突时 git 会拒绝，暂存条目保持不动。</source>
        <translation>The changes enter the worktree immediately; if they conflict with existing edits git refuses and the stash entry stays.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="546"/>
        <source>取出暂存</source>
        <translation>Pop stash</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="651"/>
        <source>没有已暂存的改动。</source>
        <translation>Nothing is staged.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="669"/>
        <source>提交成功</source>
        <translation>Committed</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="673"/>
        <source>已暂存</source>
        <translation>Staged</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="675"/>
        <source>已取消暂存</source>
        <translation>Unstaged</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="679"/>
        <source>已删除，备份在 %1</source>
        <translation>Deleted; backed up to %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="680"/>
        <source>已丢弃改动</source>
        <translation>Changes discarded</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="683"/>
        <source>已改写上次提交</source>
        <translation>Last commit rewritten</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="683"/>
        <source>已撤销上次提交，改动退回暂存区</source>
        <translation>Last commit undone; changes are back in the staging area</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="687"/>
        <source>改动已暂存到 stash</source>
        <translation>Changes were stashed</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="690"/>
        <source>已取出暂存</source>
        <translation>Stash popped</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="768"/>
        <source>%1 项改动</source>
        <translation>%1 changes</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="782"/>
        <source>查看分支与远端</source>
        <translation>View branches and remotes</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="796"/>
        <source>刷新 Git 状态</source>
        <translation>Refresh Git status</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="808"/>
        <source>改动</source>
        <translation>Changes</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="818"/>
        <source>历史</source>
        <translation>History</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="840"/>
        <source>本地操作</source>
        <translation>Local actions</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="855"/>
        <source>撤销最后一次提交（改动退回暂存区）</source>
        <translation>Undo the last commit (changes return to staging)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="868"/>
        <source>改写最后一次提交（并入已暂存改动）</source>
        <translation>Rewrite the last commit (fold in staged changes)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="881"/>
        <source>把已跟踪改动收进 stash</source>
        <translation>Move tracked changes into a stash</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="894"/>
        <source>查看 stash 列表</source>
        <translation>Show the stash list</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="913"/>
        <source>正在读取 Git 状态…</source>
        <translation>Reading Git status…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="936"/>
        <source>当前会话未关联项目；在项目中打开的会话才能使用 Git 管理。</source>
        <translation>This session is not tied to a project; Git management is available in sessions opened from a project.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="955"/>
        <source>当前项目不是 Git 仓库，无法使用 Git 管理。</source>
        <translation>The current project is not a Git repository; Git management is unavailable.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="967"/>
        <source>工作区干净，没有未提交的改动。</source>
        <translation>Working tree clean — no uncommitted changes.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="979"/>
        <source>已暂存 · %1</source>
        <translation>Staged · %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="998"/>
        <source>取消暂存该文件</source>
        <translation>Unstage this file</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1008"/>
        <source>未暂存 · %1</source>
        <translation>Unstaged · %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1029"/>
        <source>暂存该文件</source>
        <translation>Stage this file</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1057"/>
        <source>提交说明（必填）</source>
        <translation>Commit message (required)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1086"/>
        <source>已暂存 %1 个文件</source>
        <translation>%1 file(s) staged</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1095"/>
        <source>AI 起草</source>
        <translation>Draft with AI</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1099"/>
        <source>用已暂存的差异起草提交说明</source>
        <translation>Draft a commit message from the staged diff</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1105"/>
        <location filename="../src/ui/components/GitPanel.qml" line="1669"/>
        <source>提交</source>
        <translation>Commit</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1116"/>
        <source>正在起草提交说明…</source>
        <translation>Drafting the commit message…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1126"/>
        <source>正在提交…</source>
        <translation>Committing…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1126"/>
        <source>正在更新暂存区…</source>
        <translation>Updating the index…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1164"/>
        <source>正在读取 stash…</source>
        <translation>Reading stash…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1184"/>
        <source>stash 是空的。</source>
        <translation>The stash is empty.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1237"/>
        <source>取出</source>
        <translation>Pop</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1274"/>
        <source>搜索提交说明</source>
        <translation>Search commit messages</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1287"/>
        <source>作者</source>
        <translation>Author</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1297"/>
        <source>图</source>
        <translation>Graph</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1301"/>
        <source>打开多分支提交图</source>
        <translation>Open the multi-branch commit graph</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1307"/>
        <source>筛选</source>
        <translation>Filter</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1322"/>
        <source>清除筛选条件</source>
        <translation>Clear the filter</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1336"/>
        <source>没有符合条件的提交。</source>
        <translation>No commits match the filter.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1337"/>
        <source>筛选后显示 %1 条（关键词 %2，作者 %3）</source>
        <translation>Showing %1 filtered commits (keyword %2, author %3)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1354"/>
        <source>对比基准 A：%1</source>
        <translation>Comparison base A: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1372"/>
        <source>正在读取 Git 历史…</source>
        <translation>Reading Git history…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1392"/>
        <source>仓库还没有提交。</source>
        <translation>This repository has no commits yet.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1424"/>
        <source>查看提交详情</source>
        <translation>View commit details</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1486"/>
        <source>取消对比基准</source>
        <translation>Cancel the comparison base</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1486"/>
        <source>选为对比基准 / 与基准对比</source>
        <translation>Set as base / compare with the base</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1495"/>
        <source>加载更多提交</source>
        <translation>Load more commits</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1597"/>
        <source>提交已暂存的改动？</source>
        <translation>Commit the staged changes?</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1606"/>
        <source>将提交 %1 个文件：</source>
        <translation>%1 file(s) will be committed:</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitPanel.qml" line="1363"/>
        <location filename="../src/ui/components/GitPanel.qml" line="1663"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
</context>
<context>
    <name>GitWorkingDiffDialog</name>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="39"/>
        <source>未跟踪</source>
        <translation>Untracked</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="40"/>
        <source>已暂存</source>
        <translation>Staged</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="40"/>
        <source>未暂存</source>
        <translation>Unstaged</translation>
    </message>
    <message>
        <source>未跟踪文件：暂不展示内容差异，暂存后可查看。</source>
        <translation type="vanished">Untracked file: no diff yet. Stage it to view the diff.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="51"/>
        <source>未跟踪文件没有可展示的内容。</source>
        <translation>The untracked file has no content to show.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="53"/>
        <source>没有已暂存的差异。</source>
        <translation>No staged diff.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="54"/>
        <source>没有未暂存的改动。</source>
        <translation>No unstaged changes.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="104"/>
        <source>无法解析该文件的绝对路径。</source>
        <translation>Cannot resolve this file&apos;s absolute path.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="108"/>
        <source>已复制路径</source>
        <translation>Path copied</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="113"/>
        <source>已用系统默认程序打开</source>
        <translation>Opened with the default app</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="116"/>
        <source>文件不存在，或系统没有可用的打开方式。</source>
        <translation>The file does not exist, or no app is registered for it.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="147"/>
        <source>已把该块退回未暂存</source>
        <translation>Hunk returned to unstaged</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="147"/>
        <source>已暂存该块</source>
        <translation>Hunk staged</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="200"/>
        <source>用系统默认程序打开</source>
        <translation>Open with the default app</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="208"/>
        <source>复制绝对路径</source>
        <translation>Copy absolute path</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="216"/>
        <source>查看该文件历史</source>
        <translation>View this file&apos;s history</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="225"/>
        <source>查看该文件逐行归属</source>
        <translation>View line attribution for this file</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="239"/>
        <source>删除该未跟踪文件（先备份）</source>
        <translation>Delete this untracked file (backed up first)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="240"/>
        <source>丢弃该文件的工作区改动</source>
        <translation>Discard this file&apos;s worktree changes</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="246"/>
        <source>统一</source>
        <translation>Unified</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="256"/>
        <source>并排</source>
        <translation>Side by side</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="268"/>
        <source>重新读取该文件差异</source>
        <translation>Reload this file&apos;s diff</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="275"/>
        <source>关闭工作区差异</source>
        <translation>Close working tree diff</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="302"/>
        <source>正在读取工作区差异…</source>
        <translation>Reading working tree diff…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="321"/>
        <source>未跟踪文件：按当前内容展示全部新增行。</source>
        <translation>Untracked file: all lines are shown as additions from its current content.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="345"/>
        <source>分块暂存 · %1 块</source>
        <translation>Stage by hunk · %1 hunks</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="346"/>
        <source>分块暂存</source>
        <translation>Stage by hunk</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="355"/>
        <source>正在解析差异块…</source>
        <translation>Splitting the diff…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="364"/>
        <source>收起</source>
        <translation>Collapse</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="364"/>
        <source>展开</source>
        <translation>Expand</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="445"/>
        <source>取消暂存</source>
        <translation>Unstage</translation>
    </message>
    <message>
        <location filename="../src/ui/components/GitWorkingDiffDialog.qml" line="446"/>
        <source>暂存</source>
        <translation>Stage</translation>
    </message>
</context>
<context>
    <name>InputConsole</name>
    <message>
        <location filename="../src/ui/components/InputConsole.qml" line="302"/>
        <source>附件将随消息发送给当前模型服务</source>
        <translation>Attachments are sent to the current model service with your message</translation>
    </message>
    <message>
        <source>输入消息... (Enter 发送, Shift+Enter 换行)</source>
        <translation type="vanished">Type a message... (Enter to send, Shift+Enter for newline)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/InputConsole.qml" line="359"/>
        <source>消息输入框</source>
        <translation>Message input field</translation>
    </message>
    <message>
        <source>输入消息后按 Enter 发送，Shift+Enter 换行</source>
        <translation type="vanished">Type a message and press Enter to send, Shift+Enter for new line</translation>
    </message>
    <message>
        <source>未绑定项目，无法解析引用</source>
        <translation type="vanished">No project is bound, so references cannot be resolved</translation>
    </message>
    <message>
        <source>项目内未找到该文件</source>
        <translation type="vanished">File not found in the project</translation>
    </message>
    <message>
        <source>敏感文件不可引用</source>
        <translation type="vanished">Sensitive files cannot be referenced</translation>
    </message>
    <message>
        <source>路径不在项目目录内</source>
        <translation type="vanished">Path is outside the project directory</translation>
    </message>
    <message>
        <source>无法引用</source>
        <translation type="vanished">Cannot reference this file</translation>
    </message>
    <message>
        <source>引用</source>
        <translation type="vanished">References</translation>
    </message>
    <message>
        <source>%1（%2）</source>
        <translation type="vanished">%1 (%2)</translation>
    </message>
    <message>
        <source>该文件将随消息发送：%1</source>
        <translation type="vanished">This file will be sent with the message: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/InputConsole.qml" line="353"/>
        <source>输入消息…（Enter 发送，Shift+Enter 换行，@ 引用项目文件）</source>
        <translation>Type a message… (Enter to send, Shift+Enter for a new line, @ to reference project files)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/InputConsole.qml" line="360"/>
        <source>输入消息后按 Enter 发送，Shift+Enter 换行；输入 @ 可引用项目文件</source>
        <translation>Type a message and press Enter to send, Shift+Enter for a new line; type @ to reference project files</translation>
    </message>
    <message>
        <location filename="../src/ui/components/InputConsole.qml" line="541"/>
        <source>添加附件</source>
        <translation>Add attachment</translation>
    </message>
    <message>
        <location filename="../src/ui/components/InputConsole.qml" line="542"/>
        <source>上传图片或文件附件</source>
        <translation>Upload image or file attachment</translation>
    </message>
    <message>
        <location filename="../src/ui/components/InputConsole.qml" line="593"/>
        <source>停止</source>
        <translation>Stop</translation>
    </message>
    <message>
        <location filename="../src/ui/components/InputConsole.qml" line="593"/>
        <source>发送</source>
        <translation>Send</translation>
    </message>
    <message>
        <location filename="../src/ui/components/InputConsole.qml" line="664"/>
        <source>上传图片</source>
        <translation>Upload image</translation>
    </message>
    <message>
        <location filename="../src/ui/components/InputConsole.qml" line="669"/>
        <source>上传文件</source>
        <translation>Upload file</translation>
    </message>
    <message>
        <location filename="../src/ui/components/InputConsole.qml" line="683"/>
        <source>文本文件 (*.txt *.md *.json *.csv *.log *.xml *.yml *.yaml *.ini *.cpp *.h *.py *.js *.ts)</source>
        <translation>Text files (*.txt *.md *.json *.csv *.log *.xml *.yml *.yaml *.ini *.cpp *.h *.py *.js *.ts)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/InputConsole.qml" line="684"/>
        <source>全部文件 (*)</source>
        <translation>All files (*)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/InputConsole.qml" line="721"/>
        <source>当前会话未绑定项目，无法引用项目文件。
请先在侧栏选择或导入项目。</source>
        <translation>This conversation is not bound to a project, so project files cannot be referenced.
Select or import a project in the sidebar first.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/InputConsole.qml" line="723"/>
        <source>正在扫描项目文件…</source>
        <translation>Scanning project files…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/InputConsole.qml" line="724"/>
        <source>没有匹配的项目文件</source>
        <translation>No matching project files</translation>
    </message>
    <message>
        <location filename="../src/ui/components/InputConsole.qml" line="676"/>
        <source>图片文件 (*.png *.jpg *.jpeg *.webp *.bmp *.gif)</source>
        <translation>Image files (*.png *.jpg *.jpeg *.webp *.bmp *.gif)</translation>
    </message>
</context>
<context>
    <name>InterfacePage</name>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="12"/>
        <source>界面设置</source>
        <translation>Interface</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="72"/>
        <source>外观</source>
        <translation>Appearance</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="99"/>
        <source>字体</source>
        <translation>Fonts</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="132"/>
        <location filename="../src/ui/pages/InterfacePage.qml" line="138"/>
        <source>正文字体</source>
        <translation>Body Font</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="147"/>
        <location filename="../src/ui/pages/InterfacePage.qml" line="153"/>
        <source>等宽字体</source>
        <translation>Monospace Font</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="162"/>
        <location filename="../src/ui/pages/InterfacePage.qml" line="169"/>
        <location filename="../src/ui/pages/InterfacePage.qml" line="180"/>
        <source>基础字号</source>
        <translation>Base Font Size</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="207"/>
        <source>预览（保存前生效于界面）</source>
        <translation>Preview (applied to the interface before saving)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="214"/>
        <source>正文预览：智能体正在思考多行文本 Aa Bb 0123</source>
        <translation>Body preview: The agent is thinking with multi-line text Aa Bb 0123</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="234"/>
        <source>语言</source>
        <translation>Language</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="238"/>
        <location filename="../src/ui/pages/InterfacePage.qml" line="243"/>
        <source>界面语言</source>
        <translation>Interface Language</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="59"/>
        <location filename="../src/ui/pages/InterfacePage.qml" line="249"/>
        <source>跟随系统</source>
        <translation>Follow System</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="10"/>
        <source>界面设置页</source>
        <translation>Interface settings page</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="60"/>
        <source>深色</source>
        <translation>Dark</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="61"/>
        <source>浅色</source>
        <translation>Light</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="76"/>
        <location filename="../src/ui/pages/InterfacePage.qml" line="82"/>
        <source>主题模式</source>
        <translation>Theme mode</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="83"/>
        <location filename="../src/ui/pages/InterfacePage.qml" line="244"/>
        <source>当前：%1</source>
        <translation>Current: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="103"/>
        <location filename="../src/ui/pages/InterfacePage.qml" line="111"/>
        <source>搜索字体</source>
        <translation>Search fonts</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="108"/>
        <source>按名称筛选字体</source>
        <translation>Filter fonts by name</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="112"/>
        <source>同时筛选正文字体与等宽字体</source>
        <translation>Filters both body and monospace fonts</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="121"/>
        <source>没有匹配「%1」的字体。</source>
        <translation>No fonts match “%1”.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="253"/>
        <source>简体中文</source>
        <translation>简体中文</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="254"/>
        <source>English</source>
        <translation>English</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="277"/>
        <source>窗口布局</source>
        <translation>Window Layout</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="281"/>
        <location filename="../src/ui/pages/InterfacePage.qml" line="288"/>
        <source>侧边栏默认宽度</source>
        <translation>Default Sidebar Width</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="299"/>
        <location filename="../src/ui/pages/InterfacePage.qml" line="304"/>
        <source>面板默认宽度</source>
        <translation>Default Panel Width</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="315"/>
        <location filename="../src/ui/pages/InterfacePage.qml" line="319"/>
        <source>侧边栏默认展开</source>
        <translation>Expand Sidebar by Default</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="327"/>
        <location filename="../src/ui/pages/InterfacePage.qml" line="331"/>
        <source>面板默认展开</source>
        <translation>Expand Panel by Default</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="339"/>
        <location filename="../src/ui/pages/InterfacePage.qml" line="343"/>
        <source>启动时恢复窗口</source>
        <translation>Restore Window on Startup</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="353"/>
        <location filename="../src/ui/pages/InterfacePage.qml" line="357"/>
        <source>减少动效</source>
        <translation>Reduce Motion</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/InterfacePage.qml" line="366"/>
        <source>以上默认值在下次启动时生效；当前窗口布局以手动拖拽为准。</source>
        <translation>These defaults apply on the next start; the current window layout follows manual resizing.</translation>
    </message>
</context>
<context>
    <name>KnowledgeDialog</name>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="129"/>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="150"/>
        <source>知识库</source>
        <translation>Knowledge Base</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="130"/>
        <source>管理供模型检索的本地文档</source>
        <translation>Manage the local documents the model can retrieve</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="164"/>
        <source>%1 个文档 · %2 段 · %3 字符</source>
        <translation>%1 documents · %2 chunks · %3 chars</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="200"/>
        <source>批量导入 %1/%2：正在取消…</source>
        <translation>Batch import %1/%2: cancelling…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="204"/>
        <source>批量导入 %1/%2：%3</source>
        <translation>Batch import %1/%2: %3</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="209"/>
        <source>已嵌入 %1/%2</source>
        <translation>Embedded %1/%2</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="212"/>
        <source>导入中…</source>
        <translation>Importing…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="230"/>
        <source>正在取消（等待当前文件完成）</source>
        <translation>Cancelling (waiting for the current file)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="231"/>
        <source>取消批量导入（未开始的文件将跳过）</source>
        <translation>Cancel the batch import (files not yet started are skipped)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="239"/>
        <source>导入文档</source>
        <translation>Import</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="245"/>
        <source>导入文档到知识库（可多选）</source>
        <translation>Import documents into the knowledge base (multi-select)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="254"/>
        <source>从 URL 导入</source>
        <translation>Import from URL</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="259"/>
        <source>从网页地址导入知识库</source>
        <translation>Import into the knowledge base from a web address</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="267"/>
        <source>关闭知识库</source>
        <translation>Close the knowledge base</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="308"/>
        <source>要导入的网页地址</source>
        <translation>Web address to import</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="314"/>
        <source>https://example.com/article</source>
        <translation>https://example.com/article</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="326"/>
        <source>抓取中…</source>
        <translation>Fetching…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="326"/>
        <source>抓取并导入</source>
        <translation>Fetch and import</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="337"/>
        <source>抓取网页并导入知识库</source>
        <translation>Fetch the web page and import it into the knowledge base</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="352"/>
        <source>文档内容会随提问发送给当前模型服务；如需阻止，请在设置中开启“仅本地模式”。</source>
        <translation>Document content is sent to the configured model service with your questions. To prevent that, enable &quot;local-only mode&quot; in Settings.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="366"/>
        <source>无法打开源文件（可能已被移动或删除）。</source>
        <translation>Could not open the source file (it may have been moved or deleted).</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="381"/>
        <source>重试失败的导入（%1）</source>
        <translation>Retry %1 failed import(s)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="383"/>
        <source>重试失败的批量导入</source>
        <translation>Retry the failed batch imports</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="426"/>
        <source>按名称过滤文档</source>
        <translation>Filter documents by name</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="432"/>
        <source>过滤文档…</source>
        <translation>Filter documents…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="454"/>
        <source>文档</source>
        <translation>Document</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="461"/>
        <source>段数</source>
        <translation>Chunks</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="469"/>
        <source>字符</source>
        <translation>Chars</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="477"/>
        <source>状态</source>
        <translation>Status</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="579"/>
        <source>已嵌入</source>
        <translation>Embedded</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="597"/>
        <source>降级</source>
        <translation>Degraded</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="613"/>
        <source>导入时嵌入服务不可用，已使用本地降级嵌入，检索质量有限。可点击右侧刷新按钮重新嵌入。</source>
        <translation>The embedding service was unavailable at import time, so a local fallback was used and retrieval quality is limited. Use the refresh button to re-embed.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="629"/>
        <source>打开源文件</source>
        <translation>Open the source file</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="653"/>
        <source>复制源路径</source>
        <translation>Copy the source path</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="670"/>
        <source>重新嵌入</source>
        <translation>Re-embed</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="682"/>
        <source>移除文档</source>
        <translation>Remove document</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="715"/>
        <source>没有名称匹配“%1”的文档。</source>
        <translation>No document name matches &quot;%1&quot;.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="716"/>
        <source>知识库还没有文档</source>
        <translation>No documents yet</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="726"/>
        <source>导入文本或 PDF 文件（.txt / .md / .json / .csv / .log / .pdf），
模型即可检索其内容并回答问题。</source>
        <translation>Import text or PDF files (.txt / .md / .json / .csv / .log / .pdf) and the model can search their contents to answer questions.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="744"/>
        <source>支持的文件 (*.txt *.md *.json *.csv *.log *.pdf)</source>
        <translation>Supported files (*.txt *.md *.json *.csv *.log *.pdf)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDialog.qml" line="745"/>
        <source>所有文件 (*)</source>
        <translation>All files (*)</translation>
    </message>
</context>
<context>
    <name>KnowledgeDocDialog</name>
    <message>
        <location filename="../src/ui/components/KnowledgeDocDialog.qml" line="62"/>
        <source>知识库文档详情</source>
        <translation>Knowledge base document details</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDocDialog.qml" line="63"/>
        <source>查看文档 %1 的分块预览与统计信息</source>
        <translation>View chunk previews and statistics for document %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDocDialog.qml" line="105"/>
        <source>文档已不在知识库中</source>
        <translation>Document is no longer in the knowledge base</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDocDialog.qml" line="107"/>
        <source>嵌入：本地降级</source>
        <translation>Embedding: local fallback</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDocDialog.qml" line="107"/>
        <source>嵌入：服务</source>
        <translation>Embedding: service</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDocDialog.qml" line="108"/>
        <source>%1 段 · %2 字符 · %3</source>
        <translation>%1 chunks · %2 chars · %3</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDocDialog.qml" line="129"/>
        <source>降级嵌入</source>
        <translation>Fallback embedding</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDocDialog.qml" line="160"/>
        <source>没有可显示的分块</source>
        <translation>No chunks to display</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDocDialog.qml" line="189"/>
        <source>分块 %1 / %2</source>
        <translation>Chunk %1 / %2</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDocDialog.qml" line="192"/>
        <source> · 第 %1 页</source>
        <translation> · page %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDocDialog.qml" line="203"/>
        <source>%1 字符</source>
        <translation>%1 chars</translation>
    </message>
    <message>
        <location filename="../src/ui/components/KnowledgeDocDialog.qml" line="239"/>
        <source>关闭</source>
        <translation>Close</translation>
    </message>
</context>
<context>
    <name>KnowledgePage</name>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="14"/>
        <source>知识库嵌入页</source>
        <translation>Knowledge embedding page</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="23"/>
        <source>本地 Ollama</source>
        <translation>Local Ollama</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="24"/>
        <source>OpenAI 兼容服务</source>
        <translation>OpenAI-compatible service</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="16"/>
        <source>知识库嵌入</source>
        <translation>Knowledge Embedding</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="17"/>
        <source>文档导入知识库时用这里的向量服务生成索引；修改模型或地址后需要重新索引已有文档。</source>
        <translation>This embedding service indexes documents imported into the knowledge base; after changing the model or endpoint, re-index existing documents.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="41"/>
        <source>向量服务</source>
        <translation>Vector service</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="42"/>
        <source>本地 Ollama 数据不出本机；外部服务会把文档内容发送到所配置的端点。</source>
        <translation>Local Ollama keeps data on this machine; external services send document content to the configured endpoint.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="46"/>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="63"/>
        <source>嵌入服务提供方</source>
        <translation>Embedding provider</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="81"/>
        <source>嵌入模型</source>
        <translation>Embedding Model</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="106"/>
        <source>知识库嵌入模型</source>
        <translation>Knowledge Base Embedding Model</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="113"/>
        <source>嵌入服务地址</source>
        <translation>Embedding Endpoint</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="120"/>
        <source>https://api.openai.com 或 https://api.siliconflow.cn，可带或不带 /v1</source>
        <translation>https://api.openai.com or https://api.siliconflow.cn, with or without /v1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="121"/>
        <source>http://localhost:11434</source>
        <translation>http://localhost:11434</translation>
    </message>
    <message>
        <source>API Key</source>
        <translation type="vanished">API Key</translation>
    </message>
    <message>
        <source>已配置（输入新值可覆盖，留空不修改）</source>
        <translation type="vanished">Configured (enter a new value to overwrite, leave blank to keep)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="149"/>
        <source>未配置</source>
        <translation>Not configured</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="140"/>
        <source>向量模型 API Key</source>
        <translation>Embedding API key</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="149"/>
        <source>已配置</source>
        <translation>Configured</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="152"/>
        <source>输入新值可覆盖，留空不修改</source>
        <translation>Enter a new value to overwrite; leave blank to keep the current one</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="153"/>
        <source>输入外部向量服务 API Key</source>
        <translation>Enter the external embedding service API key</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="158"/>
        <source>清除密钥</source>
        <translation>Clear key</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="161"/>
        <source>清除向量模型 API Key</source>
        <translation>Clear embedding API key</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="168"/>
        <source>文档内容将发送到所配置的外部服务生成向量，请勿导入敏感资料。</source>
        <translation>Document content will be sent to the configured external service to generate embeddings. Do not import sensitive material.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/KnowledgePage.qml" line="180"/>
        <source>纯本地模式已开启：外部向量请求将被拦截，并自动回退本地 Ollama。</source>
        <translation>Local-only mode is on: external embedding requests are blocked and fall back to local Ollama.</translation>
    </message>
</context>
<context>
    <name>LineTrendChart</name>
    <message>
        <location filename="../src/ui/components/charts/LineTrendChart.qml" line="31"/>
        <source>Token</source>
        <translation>Token</translation>
    </message>
    <message>
        <location filename="../src/ui/components/charts/LineTrendChart.qml" line="102"/>
        <source>暂无数据</source>
        <translation>No data</translation>
    </message>
    <message>
        <location filename="../src/ui/components/charts/LineTrendChart.qml" line="105"/>
        <location filename="../src/ui/components/charts/LineTrendChart.qml" line="319"/>
        <source>%1: %2 %3</source>
        <translation>%1: %2 %3</translation>
    </message>
    <message>
        <location filename="../src/ui/components/charts/LineTrendChart.qml" line="107"/>
        <source>共 %1 天，峰值 %2 %3</source>
        <translation>%1 day(s), peak %2 %3</translation>
    </message>
    <message>
        <location filename="../src/ui/components/charts/LineTrendChart.qml" line="180"/>
        <source>该范围内暂无数据</source>
        <translation>No data in this range</translation>
    </message>
    <message>
        <location filename="../src/ui/components/charts/LineTrendChart.qml" line="336"/>
        <source>每日 Token 走势折线图</source>
        <translation>Daily token trend line chart</translation>
    </message>
</context>
<context>
    <name>LogPanel</name>
    <message>
        <location filename="../src/ui/components/LogPanel.qml" line="28"/>
        <source>运行日志面板</source>
        <translation>Runtime log panel</translation>
    </message>
    <message>
        <location filename="../src/ui/components/LogPanel.qml" line="29"/>
        <source>查看应用运行日志，支持按类别、级别和关键词筛选</source>
        <translation>View application runtime logs, supports filtering by category, level and keyword</translation>
    </message>
    <message>
        <location filename="../src/ui/components/LogPanel.qml" line="35"/>
        <source>运行日志</source>
        <translation>Logs</translation>
    </message>
    <message>
        <location filename="../src/ui/components/LogPanel.qml" line="56"/>
        <source>全部</source>
        <translation>All</translation>
    </message>
    <message>
        <location filename="../src/ui/components/LogPanel.qml" line="57"/>
        <source>用户操作</source>
        <translation>User Actions</translation>
    </message>
    <message>
        <location filename="../src/ui/components/LogPanel.qml" line="58"/>
        <source>网络</source>
        <translation>Network</translation>
    </message>
    <message>
        <location filename="../src/ui/components/LogPanel.qml" line="59"/>
        <source>Agent</source>
        <translation>Agent</translation>
    </message>
    <message>
        <location filename="../src/ui/components/LogPanel.qml" line="60"/>
        <source>工具</source>
        <translation>Tools</translation>
    </message>
    <message>
        <location filename="../src/ui/components/LogPanel.qml" line="61"/>
        <source>知识库</source>
        <translation>Knowledge Base</translation>
    </message>
    <message>
        <location filename="../src/ui/components/LogPanel.qml" line="62"/>
        <source>应用</source>
        <translation>App</translation>
    </message>
    <message>
        <location filename="../src/ui/components/LogPanel.qml" line="83"/>
        <source>全部 (All)</source>
        <translation>All</translation>
    </message>
    <message>
        <location filename="../src/ui/components/LogPanel.qml" line="84"/>
        <source>调试 (Debug)</source>
        <translation>Debug</translation>
    </message>
    <message>
        <location filename="../src/ui/components/LogPanel.qml" line="85"/>
        <source>信息 (Info)</source>
        <translation>Info</translation>
    </message>
    <message>
        <location filename="../src/ui/components/LogPanel.qml" line="86"/>
        <source>警告 (Warning)</source>
        <translation>Warning</translation>
    </message>
    <message>
        <location filename="../src/ui/components/LogPanel.qml" line="87"/>
        <source>严重 (Critical)</source>
        <translation>Critical</translation>
    </message>
    <message>
        <location filename="../src/ui/components/LogPanel.qml" line="107"/>
        <source>全部日期</source>
        <translation>All dates</translation>
    </message>
    <message>
        <location filename="../src/ui/components/LogPanel.qml" line="140"/>
        <source>关键词过滤</source>
        <translation>Filter by keyword</translation>
    </message>
    <message>
        <source>历史</source>
        <translation type="vanished">History</translation>
    </message>
    <message>
        <location filename="../src/ui/components/LogPanel.qml" line="303"/>
        <source>回到底部</source>
        <translation>Scroll to bottom</translation>
    </message>
    <message>
        <location filename="../src/ui/components/LogPanel.qml" line="319"/>
        <source>复制可见日志</source>
        <translation>Copy Visible Logs</translation>
    </message>
    <message>
        <location filename="../src/ui/components/LogPanel.qml" line="324"/>
        <source>打开日志目录</source>
        <translation>Open Log Folder</translation>
    </message>
    <message>
        <location filename="../src/ui/components/LogPanel.qml" line="331"/>
        <source>清除视图</source>
        <translation>Clear View</translation>
    </message>
</context>
<context>
    <name>LogsPage</name>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="13"/>
        <source>日志与诊断页</source>
        <translation>Logs and diagnostics page</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="52"/>
        <source>日志与诊断</source>
        <translation>Logs &amp; Diagnostics</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="53"/>
        <source>日志可能包含本地路径和错误上下文，请选择安全的保存位置。</source>
        <translation>Logs may contain local paths and error context; choose a safe location.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="57"/>
        <source>日志</source>
        <translation>Logs</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="58"/>
        <source>达到单文件大小上限后自动轮转；只保留最新的指定数量文件。</source>
        <translation>Files rotate automatically at the size limit; only the newest files are kept.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="62"/>
        <source>最低记录级别</source>
        <translation>Minimum log level</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="63"/>
        <source>低于该级别的日志不会写入文件；排查问题时临时调到 debug。</source>
        <translation>Messages below this level are not written to the file; switch to debug temporarily when troubleshooting.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="82"/>
        <source>日志级别</source>
        <translation>Log Level</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="94"/>
        <source>日志目录</source>
        <translation>Log directory</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="107"/>
        <source>当前日志目录</source>
        <translation>Current log directory</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="112"/>
        <source>选择目录</source>
        <translation>Choose directory</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="113"/>
        <source>恢复默认目录</source>
        <translation>Restore default directory</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="114"/>
        <source>打开目录</source>
        <translation>Open directory</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="157"/>
        <source>最近日志</source>
        <translation>Recent log</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="158"/>
        <source>只读取日志文件的尾部（而不是整个文件），用于在应用内确认刚才发生了什么。修改级别/行数/关键字后立即生效。</source>
        <translation>Reads only the tail of the log file (not the whole file) so you can see what just happened inside the app. Changes to the level, line count or keyword take effect immediately.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="169"/>
        <source>全部级别</source>
        <translation>All levels</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="170"/>
        <source>调试及以上</source>
        <translation>Debug and above</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="171"/>
        <source>信息及以上</source>
        <translation>Info and above</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="172"/>
        <source>警告及以上</source>
        <translation>Warning and above</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="173"/>
        <source>严重</source>
        <translation>Critical</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="182"/>
        <source>日志级别过滤</source>
        <translation>Log level filter</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="190"/>
        <location filename="../src/ui/pages/LogsPage.qml" line="191"/>
        <location filename="../src/ui/pages/LogsPage.qml" line="192"/>
        <location filename="../src/ui/pages/LogsPage.qml" line="193"/>
        <source>%1 行</source>
        <translation>%1 lines</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="202"/>
        <source>显示行数</source>
        <translation>Lines shown</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="210"/>
        <source>关键字过滤（大小写不敏感）</source>
        <translation>Keyword filter (case-insensitive)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="217"/>
        <source>关键字过滤</source>
        <translation>Keyword filter</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="222"/>
        <source>刷新</source>
        <translation>Refresh</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="224"/>
        <source>重新读取最近日志</source>
        <translation>Read the recent log again</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="228"/>
        <source>跟随最新</source>
        <translation>Follow latest</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="238"/>
        <source>每 2 秒刷新并滚动到最新一行</source>
        <translation>Refresh every 2 seconds and scroll to the newest line</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="248"/>
        <source>尚未读取日志。</source>
        <translation>Log not read yet.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="251"/>
        <source>%1 MB</source>
        <translation>%1 MB</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="252"/>
        <source>%1 KB</source>
        <translation>%1 KB</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="253"/>
        <source>显示最近 %1 行（尾部块内匹配 %2 行，读取 %3）</source>
        <translation>Showing the last %1 lines (%2 matched in the tail block, %3 read)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="258"/>
        <source>；更早的内容未载入</source>
        <translation>; earlier content not loaded</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="302"/>
        <source>当前过滤条件下没有日志行。</source>
        <translation>No log lines match the current filter.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="317"/>
        <source>正在读取的日志文件：%1</source>
        <translation>Log file being read: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="323"/>
        <source>诊断包</source>
        <translation>Diagnostics bundle</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="324"/>
        <source>把脱敏设置、最近的日志和环境信息放进一个目录，反馈问题时附上即可。密钥字段与 MCP 环境变量不会写入。</source>
        <translation>Puts redacted settings, recent logs and environment details into one folder — attach it when reporting a problem. Key fields and MCP environment variables are never written.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="332"/>
        <source>导出诊断包</source>
        <translation>Export diagnostics</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="334"/>
        <source>选择目录并导出诊断包</source>
        <translation>Choose a folder and export the diagnostics bundle</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="372"/>
        <source>选择日志保存目录</source>
        <translation>Choose log directory</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="378"/>
        <source>选择诊断包保存位置</source>
        <translation>Choose where to save the diagnostics bundle</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="384"/>
        <source>已导出：%1</source>
        <translation>Exported: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="385"/>
        <source>导出失败。</source>
        <translation>Export failed.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="125"/>
        <source>单个日志文件上限（MB）</source>
        <translation>Maximum size per log file (MB)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="126"/>
        <source>达到上限后自动创建新文件。</source>
        <translation>A new file is created automatically at the limit.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="134"/>
        <source>单个日志文件大小上限，兆字节</source>
        <translation>Maximum log file size in megabytes</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="140"/>
        <location filename="../src/ui/pages/LogsPage.qml" line="149"/>
        <source>保留日志文件数</source>
        <translation>Number of log files to keep</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/LogsPage.qml" line="141"/>
        <source>轮转后只保留最新的指定数量文件，更早的自动删除。</source>
        <translation>After rotation only the newest files are kept; older ones are deleted.</translation>
    </message>
</context>
<context>
    <name>McpPage</name>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="28"/>
        <source>MCP 服务器</source>
        <translation>MCP Servers</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="29"/>
        <source>接入外部工具服务器；其工具经确认后由智能体调用。修改即时生效。</source>
        <translation>Connect external tool servers; their tools are called by the agent after confirmation. Changes take effect immediately.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="54"/>
        <source>导出配置…</source>
        <translation>Export config…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="57"/>
        <location filename="../src/ui/pages/McpPage.qml" line="250"/>
        <source>导出 MCP 服务器配置</source>
        <translation>Export MCP server config</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="62"/>
        <source>导入配置…</source>
        <translation>Import config…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="64"/>
        <location filename="../src/ui/pages/McpPage.qml" line="310"/>
        <source>导入 MCP 服务器配置</source>
        <translation>Import MCP server config</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="69"/>
        <source>添加服务器…</source>
        <translation>Add server…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="139"/>
        <source>%1 个工具</source>
        <translation>%1 tools</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="159"/>
        <source>编辑服务器</source>
        <translation>Edit server</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="169"/>
        <source>重新连接</source>
        <translation>Reconnect</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="218"/>
        <source>工具：%1</source>
        <translation>Tools: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="253"/>
        <location filename="../src/ui/pages/McpPage.qml" line="272"/>
        <source>JSON 文件 (*.json)</source>
        <translation>JSON files (*.json)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="258"/>
        <source>导出失败。</source>
        <translation>Export failed.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="262"/>
        <source>已导出 %1 台服务器；其中 %2 台的环境变量/请求头未包含，请手动补全。</source>
        <translation>Exported %1 server(s); environment variables/headers for %2 of them were not included — please fill them in manually.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="264"/>
        <source>已导出 %1 台服务器；导入后默认禁用，需要手动启用。</source>
        <translation>Exported %1 server(s). Imported servers stay disabled until you enable them.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="270"/>
        <source>选择 MCP 服务器配置文件</source>
        <translation>Select an MCP server config file</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="279"/>
        <location filename="../src/ui/pages/McpPage.qml" line="455"/>
        <source>导入失败。</source>
        <translation>Import failed.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="322"/>
        <source>共 %1 台：可导入 %2 台，同名冲突 %3 台，%4 台包含环境变量或请求头。导入的服务器默认禁用。</source>
        <translation>%1 server(s) in total: %2 importable, %3 with name conflicts, %4 containing environment variables or headers. Imported servers stay disabled.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="340"/>
        <source>同名服务器</source>
        <translation>Same-name servers</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="351"/>
        <source>跳过同名（推荐）</source>
        <translation>Skip duplicates (recommended)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="352"/>
        <source>覆盖同名</source>
        <translation>Overwrite duplicates</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="353"/>
        <source>重命名后添加</source>
        <translation>Add with renamed copies</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="359"/>
        <source>同名服务器处理方式</source>
        <translation>Duplicate name handling</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="394"/>
        <source>（未命名）</source>
        <translation>(unnamed)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="404"/>
        <source>同名</source>
        <translation>Duplicate</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="446"/>
        <source>导入</source>
        <translation>Import</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="453"/>
        <source>已导入：新增 %1 台，覆盖 %2 台，跳过 %3 台；导入的服务器默认禁用。</source>
        <translation>Imported: %1 added, %2 replaced, %3 skipped. Imported servers stay disabled.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="489"/>
        <source>移除服务器 %1？</source>
        <translation>Remove server %1?</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="441"/>
        <location filename="../src/ui/pages/McpPage.qml" line="503"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="507"/>
        <source>移除</source>
        <translation>Remove</translation>
    </message>
    <message>
        <source>添加服务器</source>
        <translation type="vanished">Add Server</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="15"/>
        <source>MCP 服务器管理页</source>
        <translation>MCP server management page</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="148"/>
        <source>启用服务器 %1</source>
        <translation>Enable server %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="180"/>
        <source>删除服务器</source>
        <translation>Delete server</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/McpPage.qml" line="236"/>
        <source>多个服务器连接失败，详见日志</source>
        <translation>Multiple servers failed to connect, see logs</translation>
    </message>
    <message>
        <source>名称</source>
        <translation type="vanished">Name</translation>
    </message>
    <message>
        <source>my-server</source>
        <translation type="vanished">my-server</translation>
    </message>
    <message>
        <source>类型</source>
        <translation type="vanished">Type</translation>
    </message>
    <message>
        <source>命令与参数（空格分隔）</source>
        <translation type="vanished">Command and arguments (space-separated)</translation>
    </message>
    <message>
        <source>npx -y @modelcontextprotocol/server-filesystem D:/docs</source>
        <translation type="vanished">npx -y @modelcontextprotocol/server-filesystem D:/docs</translation>
    </message>
    <message>
        <source>服务端点 URL</source>
        <translation type="vanished">Endpoint URL</translation>
    </message>
    <message>
        <source>https://mcp.example.com/mcp</source>
        <translation type="vanished">https://mcp.example.com/mcp</translation>
    </message>
    <message>
        <source>添加并连接</source>
        <translation type="vanished">Add &amp; Connect</translation>
    </message>
</context>
<context>
    <name>McpServerEditor</name>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="100"/>
        <source>请填写服务器名称。</source>
        <translation>Enter a server name.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="105"/>
        <source>已存在同名服务器：%1</source>
        <translation>A server named %1 already exists.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="111"/>
        <source>请填写启动命令。</source>
        <translation>Enter the launch command.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="116"/>
        <source>环境变量第 %1 行格式不对，应为 KEY=VALUE。</source>
        <translation>Environment variable line %1 is malformed; expected KEY=VALUE.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="121"/>
        <source>请填写以 http:// 或 https:// 开头的端点 URL。</source>
        <translation>Enter an endpoint URL starting with http:// or https://.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="125"/>
        <source>请求头第 %1 行格式不对，应为 Header: Value。</source>
        <translation>Header line %1 is malformed; expected Header: Value.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="133"/>
        <source>保存失败：%1</source>
        <translation>Save failed: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="134"/>
        <source>未知错误</source>
        <translation>Unknown error</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="152"/>
        <source>编辑服务器</source>
        <translation>Edit server</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="152"/>
        <source>添加服务器</source>
        <translation>Add server</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="171"/>
        <location filename="../src/ui/components/McpServerEditor.qml" line="175"/>
        <source>名称</source>
        <translation>Name</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="176"/>
        <source>my-server</source>
        <translation>my-server</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="189"/>
        <location filename="../src/ui/components/McpServerEditor.qml" line="192"/>
        <source>类型</source>
        <translation>Transport</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="208"/>
        <location filename="../src/ui/components/McpServerEditor.qml" line="212"/>
        <source>命令与参数（空格分隔）</source>
        <translation>Command and arguments (space separated)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="213"/>
        <source>npx -y @modelcontextprotocol/server-filesystem D:/docs</source>
        <translation>npx -y @modelcontextprotocol/server-filesystem D:/docs</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="223"/>
        <source>环境变量（每行 KEY=VALUE，可留空）</source>
        <translation>Environment variables (one KEY=VALUE per line; optional)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="228"/>
        <source>环境变量</source>
        <translation>Environment variables</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="229"/>
        <source>TOKEN=abc123</source>
        <translation>TOKEN=abc123</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="250"/>
        <location filename="../src/ui/components/McpServerEditor.qml" line="254"/>
        <source>服务端点 URL</source>
        <translation>Server endpoint URL</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="255"/>
        <source>https://mcp.example.com/mcp</source>
        <translation>https://mcp.example.com/mcp</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="265"/>
        <source>请求头（每行 Header: Value，可留空）</source>
        <translation>Headers (one Header: Value per line; optional)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="270"/>
        <source>请求头</source>
        <translation>Headers</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="271"/>
        <source>Authorization: Bearer &lt;token&gt;</source>
        <translation>Authorization: Bearer &lt;token&gt;</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="310"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="315"/>
        <source>保存</source>
        <translation>Save</translation>
    </message>
    <message>
        <location filename="../src/ui/components/McpServerEditor.qml" line="315"/>
        <source>添加并连接</source>
        <translation>Add and connect</translation>
    </message>
</context>
<context>
    <name>MemoryPage</name>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="13"/>
        <source>记忆管理页</source>
        <translation>Memory Management Page</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="14"/>
        <source>浏览、搜索和删除长期记忆摘要</source>
        <translation>Browse, search and delete long-term memory summaries</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="65"/>
        <source>记忆管理</source>
        <translation>Memory Management</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="66"/>
        <source>长期记忆摘要由会话自动生成，可在新会话中检索注入；删除后不可恢复。</source>
        <translation>Long-term memory summaries are auto-generated by conversations and can be retrieved and injected into new conversations; deletion is irreversible.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="76"/>
        <source>记忆与保留</source>
        <translation>Memory and retention</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="80"/>
        <location filename="../src/ui/pages/MemoryPage.qml" line="86"/>
        <source>启用长期记忆</source>
        <translation>Enable long-term memory</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="81"/>
        <source>开启后，每次任务结束会生成会话摘要并在新会话中注入相关历史，帮助模型记住此前聊过的内容。</source>
        <translation>When enabled, a session summary is generated after each task and relevant history is injected into new sessions, helping the model remember what was discussed before.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="92"/>
        <location filename="../src/ui/pages/MemoryPage.qml" line="100"/>
        <source>审计日志保留天数</source>
        <translation>Audit log retention (days)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="93"/>
        <source>工具执行审计链的保留天数，0 表示永久保留。到期记录在启动与每 6 小时的例行清理中删除。</source>
        <translation>How many days to keep the tool-execution audit chain; 0 keeps it forever. Expired rows are removed at startup and in the routine sweep every 6 hours.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="106"/>
        <location filename="../src/ui/pages/MemoryPage.qml" line="114"/>
        <source>Token 用量保留天数</source>
        <translation>Token usage retention (days)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="107"/>
        <source>每次模型请求的用量明细（token 与费用统计的数据源）的保留天数，0 表示永久保留。</source>
        <translation>How many days to keep per-request usage records (the data source for token and cost statistics); 0 keeps them forever.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="130"/>
        <source>搜索记忆内容…</source>
        <translation>Search memory content…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="143"/>
        <source>搜索记忆内容</source>
        <translation>Search memory content</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="148"/>
        <source>清空全部</source>
        <translation>Clear All</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="151"/>
        <source>清空全部长期记忆</source>
        <translation>Clear all long-term memory</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="160"/>
        <source>共 %1 条记忆</source>
        <translation>%1 memories total</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="214"/>
        <source>(空摘要)</source>
        <translation>(empty summary)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="229"/>
        <source>收起</source>
        <translation>Collapse</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="229"/>
        <source>展开全文</source>
        <translation>Expand</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="263"/>
        <source>遗忘</source>
        <translation>Forget</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="265"/>
        <source>遗忘这条记忆</source>
        <translation>Forget this memory</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="279"/>
        <source>没有匹配的记忆</source>
        <translation>No matching memories</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="280"/>
        <source>暂无长期记忆。开启上方的长期记忆开关后，完成对话会自动生成摘要。</source>
        <translation>No long-term memory yet. Enable the long-term memory switch above; summaries are auto-generated after conversations complete.</translation>
    </message>
    <message>
        <source>暂无长期记忆。开启「工具与记忆」中的长期记忆后，完成对话会自动生成摘要。</source>
        <translation type="vanished">No long-term memory yet. Enable long-term memory in &quot;Tools &amp; Memory&quot; settings; summaries are auto-generated after conversations complete.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="297"/>
        <source>确认清空全部长期记忆？此操作不可恢复。</source>
        <translation>Confirm clearing all long-term memory? This action is irreversible.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="315"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/MemoryPage.qml" line="319"/>
        <source>清空</source>
        <translation>Clear</translation>
    </message>
</context>
<context>
    <name>MessageNavigator</name>
    <message>
        <location filename="../src/ui/components/MessageNavigator.qml" line="179"/>
        <source>第 %1 条</source>
        <translation>Message %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/MessageNavigator.qml" line="146"/>
        <source>跳到第 %1 条提问：%2</source>
        <translation>Jump to message %1: %2</translation>
    </message>
</context>
<context>
    <name>MetricsLine</name>
    <message>
        <location filename="../src/ui/components/MetricsLine.qml" line="42"/>
        <source>任务指标</source>
        <translation>Task metrics</translation>
    </message>
</context>
<context>
    <name>ModelConfigPage</name>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="19"/>
        <source>模型配置页</source>
        <translation>Model configuration page</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="72"/>
        <source>Gemini</source>
        <translation>Gemini</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="948"/>
        <source>temperature</source>
        <translation>temperature</translation>
    </message>
    <message>
        <source>最大输出</source>
        <translation type="vanished">Max Output</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="980"/>
        <source>num_ctx</source>
        <translation>num_ctx</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="1000"/>
        <source>思考强度</source>
        <translation>Thinking effort</translation>
    </message>
    <message>
        <source>上下文预算</source>
        <translation type="vanished">Context Budget</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="99"/>
        <source>例如 https://my-gateway.example.com/v1</source>
        <translation>e.g. https://my-gateway.example.com/v1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="110"/>
        <source>网关上真实在跑的模型名，例如 glm-4</source>
        <translation>Model name served by your gateway, e.g. glm-4</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="127"/>
        <source>按网关要求填写（可留空走环境变量）</source>
        <translation>Enter as your gateway requires (blank falls back to the environment variable)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="128"/>
        <source>或留空，读环境变量 %1</source>
        <translation>Or leave blank to read %1 from the environment</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="135"/>
        <source>模型名</source>
        <translation>model name</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="148"/>
        <source>手动覆盖</source>
        <translation>manual override</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="149"/>
        <source>模型能力表</source>
        <translation>model capability table</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="149"/>
        <source>全局默认</source>
        <translation>global default</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="238"/>
        <source>共 %1 个模型，新增 %2 个。</source>
        <translation>%1 models found, %2 added.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="254"/>
        <source>模型服务配置</source>
        <translation>Model Configuration</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="255"/>
        <source>点芯片切换要编辑的配置，拖动芯片可调整顺序；改动即时写入配置，不需要另点保存。</source>
        <translation>Click a chip to edit that profile; drag chips to reorder. Changes save instantly.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="515"/>
        <source>新建一条配置，填上服务地址与模型名，聊天区就能开始工作。</source>
        <translation>Create a profile with the service URL and model name, and the chat is ready to go.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="525"/>
        <source>新建第一个配置</source>
        <translation>Create your first profile</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="560"/>
        <source>厂商决定默认地址，接口协议决定报文格式；模型由你自己填。</source>
        <translation>The vendor picks the default URL and the protocol decides the wire format; you type the model yourself.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="669"/>
        <source>已配置</source>
        <translation>Configured</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="669"/>
        <source>未配置</source>
        <translation>Not configured</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="751"/>
        <source>模型名不再按厂商预填：在下面的输入框里自己添加，列表会同步到聊天输入区的选择器。</source>
        <translation>Model names are no longer preset by vendor: add your own in the input below, and the list syncs to the chat input&apos;s selector.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="888"/>
        <source>正在拉取…</source>
        <translation>Fetching…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="888"/>
        <source>从端点拉取模型</source>
        <translation>Fetch models from endpoint</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="905"/>
        <source>正在向端点请求模型列表（最多等 20 秒）…</source>
        <translation>Requesting the model list from the endpoint (up to 20 seconds)…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="917"/>
        <source>先在上方「服务地址」里填端点，才能从端点拉取模型。</source>
        <translation>Fill in the service address above before fetching models from the endpoint.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="929"/>
        <source>列表还是空的：模型名不再自动预填，在右边输入一个名字回车即可。</source>
        <translation>The list is still empty: model names are no longer auto-filled; type a name on the right and press Enter.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="960"/>
        <source>自定义上限，留空 = 服务默认</source>
        <translation>Custom limit, blank = server default</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="1026"/>
        <source>自定义预算，留空 = 按能力表</source>
        <translation>Custom budget, blank = follow capability table</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="1133"/>
        <source>删除模型配置</source>
        <translation>Delete model profile</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="1143"/>
        <source>将删除「%1」，该配置的 API Key 会一并清除，操作不可撤销。</source>
        <translation>"%1" will be deleted and its API key removed with it. This cannot be undone.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="1159"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="452"/>
        <source>复制</source>
        <translation>Duplicate</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="461"/>
        <source>%1 副本</source>
        <translation>%1 copy</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="1105"/>
        <source>厂商配置会带上默认地址，模型名一律留空由你自己填——预填的官方模型列表未必是你实际调用的那个。</source>
        <translation>A vendor config comes with a default address; the model name is left blank for you to fill in - the preset official model list may not be the one you actually call.</translation>
    </message>
    <message>
        <source>可保存多个模型配置，点击芯片切换编辑，「启用」后立即生效</source>
        <translation type="vanished">Save multiple model profiles and click a chip to edit it; &quot;Enable&quot; takes effect immediately</translation>
    </message>
    <message>
        <source>本页修改实时保存，不受底部「保存 / 取消」影响</source>
        <translation type="vanished">Changes on this page save instantly and are not affected by the &quot;Save / Cancel&quot; buttons below</translation>
    </message>
    <message>
        <source>+ 添加</source>
        <translation type="vanished">+ Add</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="564"/>
        <source>显示名称</source>
        <translation>Display Name</translation>
    </message>
    <message>
        <source>点芯片切换要编辑的配置；改动即时写入配置，不需要另点保存。</source>
        <translation type="vanished">Click a chip to switch the profile being edited. Changes are written immediately; there is no separate save step.</translation>
    </message>
    <message>
        <source>一条配置对应一个可用服务。厂商决定默认值，接口协议才决定报文格式。</source>
        <translation type="vanished">One profile per service. The vendor sets the defaults; the API protocol sets the wire format.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="413"/>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="1082"/>
        <source>新建模型配置</source>
        <translation>New Model Profile</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="418"/>
        <source>+ 新建</source>
        <translation>+ New</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="565"/>
        <source>例如：工作用 GPT、本地 qwen…</source>
        <translation>e.g. Work GPT, local qwen…</translation>
    </message>
    <message>
        <source>提供商</source>
        <translation type="vanished">Provider</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="68"/>
        <source>OpenAI 兼容</source>
        <translation>OpenAI Compatible</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="69"/>
        <source>Claude</source>
        <translation>Claude</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="73"/>
        <source>Ollama 本地</source>
        <translation>Ollama (Local)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="74"/>
        <source>自定义网关</source>
        <translation>Custom Gateway</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="559"/>
        <source>配置来源</source>
        <translation>Profile Source</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="606"/>
        <source>接口协议</source>
        <translation>API Protocol</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="134"/>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="656"/>
        <source>服务地址</source>
        <translation>Base URL</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="665"/>
        <source>API Key</source>
        <translation>API Key</translation>
    </message>
    <message>
        <source>sk-...（或设置环境变量 OPENAI_API_KEY）</source>
        <translation type="vanished">sk-... (or set the OPENAI_API_KEY environment variable)</translation>
    </message>
    <message>
        <source>服务地址 Base URL</source>
        <translation type="vanished">Base URL</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="755"/>
        <source>当前模型</source>
        <translation>Current Model</translation>
    </message>
    <message>
        <source>模型列表</source>
        <translation type="vanished">Model List</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="818"/>
        <source>%1K</source>
        <translation>%1K</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="819"/>
        <source>未知</source>
        <translation>Unknown</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="857"/>
        <source>添加模型…</source>
        <translation>Add model…</translation>
    </message>
    <message>
        <source>生成参数（留空 = 使用服务默认，不随请求发送）</source>
        <translation type="vanished">Sampling Parameters (leave empty to use service defaults; not sent with requests)</translation>
    </message>
    <message>
        <source>temperature (0-2)</source>
        <translation type="vanished">temperature (0-2)</translation>
    </message>
    <message>
        <source>留空 = 不发送</source>
        <translation type="vanished">Empty = not sent</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="957"/>
        <source>最大输出 tokens</source>
        <translation>Max Output Tokens</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="593"/>
        <source>自定义不预填服务地址与模型名，也不带入厂商预设模型；请求仍按 OpenAI Chat Completions 报文发出。</source>
        <translation>A custom profile pre-fills neither the base URL nor the model name, and brings in no vendor presets. Requests still use the OpenAI Chat Completions wire format.</translation>
    </message>
    <message>
        <source>一条配置对应一个可用服务。厂商决定默认地址，接口协议决定报文格式；模型由你自己填。</source>
        <translation type="vanished">One profile maps to one usable service: the vendor decides the default address, the API protocol decides the message format, and the model is yours to fill in.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="636"/>
        <source>Responses 需要端点显式实现 /responses；多数「OpenAI 兼容」网关只提供 Chat Completions。</source>
        <translation>Responses requires the endpoint to implement /responses; most OpenAI-compatible gateways only offer Chat Completions.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="649"/>
        <source>服务连接</source>
        <translation>Connection</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="651"/>
        <source>本地服务不需要 API Key；地址指向 Ollama 的 host:port。</source>
        <translation>Local services need no API key; the address points at Ollama&apos;s host:port.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="652"/>
        <source>API Key 保存时按当前用户加密（Windows DPAPI）。</source>
        <translation>The API key is encrypted per user when stored (Windows DPAPI).</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="683"/>
        <source>密钥已填写，保存时加密存储。</source>
        <translation>Key filled in; stored encrypted.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="684"/>
        <source>密钥为空：请求前会先读环境变量 %1。</source>
        <translation>No key set: %1 from the environment is read before the request.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="717"/>
        <source>测试用模型</source>
        <translation>Test model</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="729"/>
        <source>测试用模型只影响本次请求，不会改写当前模型。</source>
        <translation>The test model only affects this request; it does not change the current model.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="730"/>
        <source>模型列表为空，没东西可测：先在上方「模型」卡片里添加一个模型名。</source>
        <translation>The model list is empty, nothing to test: add a model name in the &quot;Model&quot; card above first.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="740"/>
        <source>正在连接（最多等 20 秒）…</source>
        <translation>Connecting (waits up to 20 s)…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="750"/>
        <source>模型</source>
        <translation>Models</translation>
    </message>
    <message>
        <source>列表里的模型会同时出现在聊天输入区的选择器里；点芯片设为当前模型，× 移出列表。</source>
        <translation type="vanished">These models also appear in the chat model picker. Click a chip to make it current; × removes it.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="790"/>
        <source>设为当前模型 %1</source>
        <translation>Set as current model %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="857"/>
        <source>添加网关模型名…</source>
        <translation>Add gateway model name…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="871"/>
        <source>添加模型到列表</source>
        <translation>Add model to list</translation>
    </message>
    <message>
        <source>列表还是空的：自定义配置不带预设，在右边输入网关上的模型名后回车。</source>
        <translation type="vanished">The list is empty: a custom profile carries no presets, so type the model name your gateway serves and press Enter.</translation>
    </message>
    <message>
        <source>列表还是空的：在右边输入模型名回车即可加回来。</source>
        <translation type="vanished">The list is empty: type a model name and press Enter to add it back.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="943"/>
        <source>生成参数</source>
        <translation>Generation</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="944"/>
        <source>留空 = 使用服务默认，不随请求发送。</source>
        <translation>Blank = service default; the field is not sent.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="949"/>
        <source>0 - 2，留空不发送</source>
        <translation>0 - 2, blank to omit</translation>
    </message>
    <message>
        <source>留空 = 服务默认</source>
        <translation type="vanished">Empty = service default</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="972"/>
        <source>Ollama 的上下文窗口就是 num_ctx</source>
        <translation>For Ollama the context window is num_ctx</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="983"/>
        <source>自定义窗口，留空 = 不发送</source>
        <translation>Custom window, blank = do not send</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="995"/>
        <source>思考与上下文</source>
        <translation>Reasoning and Context</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="996"/>
        <source>思考强度存在这条配置上；聊天输入区的快捷选择框读写的是当前启用的配置。</source>
        <translation>Effort is stored on this profile; the shortcut selector by the chat input reads and writes the active profile.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="1010"/>
        <source>OpenAI 兼容按档位发送 reasoning_effort；Claude 折算为思考预算（输出上限要留得住预算，否则自动退回不思考）；Ollama 只有开/关，低中高均按开处理。</source>
        <translation>OpenAI sends reasoning_effort per level; Claude converts it into a thinking budget (the output limit must leave room for it, otherwise thinking is dropped); Ollama is on/off only, so low, medium and high all mean on.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="1023"/>
        <source>上下文预算覆盖</source>
        <translation>Context Budget Override</translation>
    </message>
    <message>
        <source>留空 = 按模型能力表自动</source>
        <translation type="vanished">Blank = follow the model capability table</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="1035"/>
        <source>当前生效 %1 tokens（来源：%2）</source>
        <translation>Effective budget: %1 tokens (source: %2)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="1037"/>
        <source>当前生效 %1 tokens（来源：全局默认，能力表里没有这个模型）</source>
        <translation>Effective budget: %1 tokens (global default; the capability table has no entry for this model)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="1052"/>
        <source>最大输出 %1 已不小于上下文窗口 %2，请求可能被服务端拒绝。</source>
        <translation>Max output %1 is not below the context window %2; the server may reject the request.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="505"/>
        <source>还没有模型配置</source>
        <translation>No model profile yet</translation>
    </message>
    <message>
        <source>✓ 正在使用：%1</source>
        <translation type="vanished">✓ Active: %1</translation>
    </message>
    <message>
        <source>编辑中的配置未启用</source>
        <translation type="vanished">The profile being edited is not active</translation>
    </message>
    <message>
        <source>已保存：%1</source>
        <translation type="vanished">Saved: %1</translation>
    </message>
    <message>
        <source>还需填写：%1，才能启用或测试连接</source>
        <translation type="vanished">Fill in %1 before you can activate this profile or test it</translation>
    </message>
    <message>
        <source>厂商配置会带上官方默认地址与模型；「自定义网关」两项都留空，由你自己填。</source>
        <translation type="vanished">Vendor profiles pre-fill the official URL and model; Custom Gateway leaves both blank for you to fill.</translation>
    </message>
    <message>
        <source>num_ctx（上下文窗口）</source>
        <translation type="vanished">num_ctx (context window)</translation>
    </message>
    <message>
        <source>思考强度（留默认 = 不随请求发送思考参数）</source>
        <translation type="vanished">Thinking effort (Default = no thinking parameter is sent)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="1004"/>
        <source>默认</source>
        <translation>Default</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="79"/>
        <source>关闭</source>
        <translation>Off</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="80"/>
        <source>低</source>
        <translation>Low</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="81"/>
        <source>中</source>
        <translation>Medium</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="82"/>
        <source>高</source>
        <translation>High</translation>
    </message>
    <message>
        <source>OpenAI 兼容按档位发送 reasoning_effort；Claude 折算为思考预算（需输出上限留得住预算，否则自动退回不思考）；Ollama 只有开/关，低中高均按开处理。</source>
        <translation type="vanished">OpenAI-compatible endpoints send reasoning_effort per level; Claude maps the level to a thinking budget (the output cap must leave room for it, otherwise thinking is dropped); Ollama only has on/off, so low/medium/high all enable thinking.</translation>
    </message>
    <message>
        <source>上下文预算覆盖（留空 = 按模型能力表自动）</source>
        <translation type="vanished">Context budget override (empty = auto by model capability)</translation>
    </message>
    <message>
        <source>留空 = 自动</source>
        <translation type="vanished">Empty = auto</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="444"/>
        <source>✓ 使用中</source>
        <translation>✓ In Use</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="444"/>
        <source>启用此配置</source>
        <translation>Enable This Profile</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="467"/>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="1165"/>
        <source>删除</source>
        <translation>Delete</translation>
    </message>
    <message>
        <source>正在连接…</source>
        <translation type="vanished">Connecting…</translation>
    </message>
    <message>
        <source>测试中…</source>
        <translation type="vanished">Testing…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ModelConfigPage.qml" line="696"/>
        <source>测试连接</source>
        <translation>Test Connection</translation>
    </message>
</context>
<context>
    <name>ModelSelector</name>
    <message>
        <location filename="../src/ui/components/ModelSelector.qml" line="35"/>
        <source>上下文窗口：%1K</source>
        <translation>Context window: %1K</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ModelSelector.qml" line="36"/>
        <source>上下文窗口：未知（使用全局默认）</source>
        <translation>Context window: unknown (using global default)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ModelSelector.qml" line="56"/>
        <source>模型选择器</source>
        <translation>Model selector</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ModelSelector.qml" line="59"/>
        <source>未配置模型</source>
        <translation>No model configured</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ModelSelector.qml" line="95"/>
        <source>未配置模型 · 点击设置</source>
        <translation>No model configured · Click to set up</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ModelSelector.qml" line="98"/>
        <location filename="../src/ui/components/ModelSelector.qml" line="327"/>
        <location filename="../src/ui/components/ModelSelector.qml" line="350"/>
        <source>未指定模型</source>
        <translation>no model set</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ModelSelector.qml" line="164"/>
        <source>模型 · %1</source>
        <translation>Model · %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ModelSelector.qml" line="254"/>
        <source>这条配置还没有可选模型：先在「管理模型配置…」里填写模型名。</source>
        <translation>This profile has no models yet - set a model name under Manage model profiles…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ModelSelector.qml" line="273"/>
        <source>配置 · 共 %1 个</source>
        <translation>Profiles · %1 total</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ModelSelector.qml" line="400"/>
        <location filename="../src/ui/components/ModelSelector.qml" line="420"/>
        <source>管理模型配置…</source>
        <translation>Manage Model Profiles…</translation>
    </message>
</context>
<context>
    <name>NetworkPage</name>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="12"/>
        <source>网络与代理页</source>
        <translation>Network and proxy page</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="14"/>
        <source>网络与代理</source>
        <translation>Network &amp; Proxy</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="15"/>
        <source>代理设置对模型服务、向量服务、联网搜索与 MCP HTTP 连接生效；本机地址始终直连。</source>
        <translation>Proxy settings apply to model, embedding, web search and MCP HTTP connections; local addresses always connect directly.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="23"/>
        <source>HTTP 代理</source>
        <translation>HTTP Proxy</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="24"/>
        <source>跟随系统会读取操作系统代理配置；手动代理对所有出站 HTTP 请求生效。</source>
        <translation>System mode uses the operating system proxy settings; manual mode applies to all outbound HTTP requests.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="28"/>
        <location filename="../src/ui/pages/NetworkPage.qml" line="44"/>
        <source>代理模式</source>
        <translation>Proxy mode</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="34"/>
        <source>跟随系统</source>
        <translation>Follow System</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="34"/>
        <source>手动代理</source>
        <translation>Manual proxy</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="34"/>
        <source>不使用代理</source>
        <translation>No proxy</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="62"/>
        <source>proxy.example.com（不含 http://）</source>
        <translation>proxy.example.com (without http://)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="56"/>
        <source>代理服务器地址</source>
        <translation>Proxy server address</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="72"/>
        <location filename="../src/ui/pages/NetworkPage.qml" line="85"/>
        <source>代理端口</source>
        <translation>Proxy port</translation>
    </message>
    <message>
        <source>用户名（可选）</source>
        <translation type="vanished">Username (optional)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="96"/>
        <source>代理认证用户名</source>
        <translation>Proxy authentication username</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="92"/>
        <source>代理用户名</source>
        <translation>Proxy username</translation>
    </message>
    <message>
        <source>密码（可选）</source>
        <translation type="vanished">Password (optional)</translation>
    </message>
    <message>
        <source>已配置（输入新值可覆盖，留空不修改）</source>
        <translation type="vanished">Configured (enter a new value to overwrite, leave blank to keep)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="116"/>
        <source>未配置</source>
        <translation>Not configured</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="108"/>
        <source>代理密码</source>
        <translation>Proxy password</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="116"/>
        <source>已配置</source>
        <translation>Configured</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="119"/>
        <source>输入新值可覆盖，留空不修改</source>
        <translation>Enter a new value to overwrite; leave blank to keep the current one</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="120"/>
        <source>输入代理密码</source>
        <translation>Enter the proxy password</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="125"/>
        <source>清除密码</source>
        <translation>Clear password</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="128"/>
        <source>清除代理密码</source>
        <translation>Clear proxy password</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="139"/>
        <source>测试代理</source>
        <translation>Test proxy</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="139"/>
        <source>测试中…</source>
        <translation>Testing…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/NetworkPage.qml" line="142"/>
        <source>测试代理连接</source>
        <translation>Test proxy connection</translation>
    </message>
</context>
<context>
    <name>NexusAI</name>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="79"/>
        <source>智能体行为</source>
        <translation>Agent Behavior</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="81"/>
        <source>工具与能力</source>
        <translation>Tools &amp; Capabilities</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="86"/>
        <source>界面与外观</source>
        <translation>Interface &amp; Appearance</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="89"/>
        <source>常规应用</source>
        <translation>General</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="93"/>
        <source>网络与代理</source>
        <translation>Network &amp; Proxy</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="95"/>
        <source>安全与隐私</source>
        <translation>Security &amp; Privacy</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="102"/>
        <source>知识库嵌入</source>
        <translation>Knowledge Embedding</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="104"/>
        <source>提示词与指令</source>
        <translation>Prompts &amp; commands</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="109"/>
        <source>用量与配额</source>
        <translation>Usage &amp; Quota</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="113"/>
        <source>日志与诊断</source>
        <translation>Logs &amp; Diagnostics</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="115"/>
        <source>备份与恢复</source>
        <translation>Backup &amp; Restore</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="119"/>
        <source>快捷键</source>
        <translation>Shortcuts</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="124"/>
        <source>数据管理</source>
        <translation>Data Management</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="138"/>
        <source>导航分组折叠状态</source>
        <translation>Navigation group collapse state</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="141"/>
        <source>主题模式</source>
        <translation>Theme Mode</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="148"/>
        <source>正文字体</source>
        <translation>Body Font</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="151"/>
        <source>等宽字体</source>
        <translation>Monospace Font</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="154"/>
        <source>基础字号</source>
        <translation>Base Font Size</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="162"/>
        <source>侧边栏默认宽度</source>
        <translation>Default Sidebar Width</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="165"/>
        <source>面板默认宽度</source>
        <translation>Default Panel Width</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="168"/>
        <source>侧边栏默认展开</source>
        <translation>Expand Sidebar by Default</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="171"/>
        <source>面板默认展开</source>
        <translation>Expand Panel by Default</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="174"/>
        <source>启动时恢复窗口</source>
        <translation>Restore Window on Startup</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="179"/>
        <source>减少动效</source>
        <translation>Reduce Motion</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="190"/>
        <source>设置窗口宽度</source>
        <translation>Settings dialog width</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="193"/>
        <source>设置窗口高度</source>
        <translation>Settings dialog height</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="208"/>
        <source>首次启动向导已完成</source>
        <translation>First-run wizard completed</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="157"/>
        <source>界面语言</source>
        <translation>Interface Language</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="63"/>
        <source>模型与对话</source>
        <translation>Models &amp; Chat</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="64"/>
        <source>外观与常规</source>
        <translation>Appearance &amp; General</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="65"/>
        <source>知识与内容</source>
        <translation>Knowledge &amp; Content</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="66"/>
        <source>数据与诊断</source>
        <translation>Data &amp; Diagnostics</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="74"/>
        <source>模型配置</source>
        <translation>Model Configuration</translation>
    </message>
    <message>
        <source>界面设置</source>
        <translation type="vanished">Interface</translation>
    </message>
    <message>
        <source>通用设置</source>
        <translation type="vanished">General</translation>
    </message>
    <message>
        <source>安全</source>
        <translation type="vanished">Security</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="83"/>
        <source>MCP 服务器</source>
        <translation>MCP Servers</translation>
    </message>
    <message>
        <source>工具管理</source>
        <translation type="vanished">Tool management</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="111"/>
        <source>活动记录</source>
        <translation>Activity Log</translation>
    </message>
    <message>
        <source>设置与备份</source>
        <translation type="vanished">Settings and Backup</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="106"/>
        <source>项目规则</source>
        <translation>Project rules</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="184"/>
        <source>自定义快捷键</source>
        <translation>Custom shortcuts</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="196"/>
        <source>关闭时最小化到系统托盘</source>
        <translation>Minimize to system tray on close</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="199"/>
        <source>开机自启动</source>
        <translation>Start with Windows</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="202"/>
        <source>系统通知</source>
        <translation>System notifications</translation>
    </message>
    <message>
        <source>工具与记忆</source>
        <translation type="vanished">Tools &amp; Memory</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="98"/>
        <source>记忆管理</source>
        <translation>Memory Management</translation>
    </message>
    <message>
        <source>用量统计</source>
        <translation type="vanished">Usage Statistics</translation>
    </message>
    <message>
        <location filename="../src/core/utils/SettingsSchema.cpp" line="133"/>
        <source>上次访问分类</source>
        <translation>Last Visited Category</translation>
    </message>
    <message>
        <location filename="../src/core/utils/LogTailReader.cpp" line="55"/>
        <source>还没有可读取的日志文件</source>
        <translation>No readable log file yet</translation>
    </message>
    <message>
        <location filename="../src/core/utils/LogTailReader.cpp" line="61"/>
        <source>日志文件不存在：%1</source>
        <translation>Log file not found: %1</translation>
    </message>
    <message>
        <location filename="../src/core/utils/LogTailReader.cpp" line="65"/>
        <source>无法读取日志文件：%1</source>
        <translation>Cannot read log file: %1</translation>
    </message>
    <message>
        <location filename="../src/core/utils/LogTailReader.cpp" line="73"/>
        <source>无法定位日志文件尾部：%1</source>
        <translation>Cannot seek to the end of the log file: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ShortcutMap.cpp" line="48"/>
        <source>打开设置中心</source>
        <translation>Open Settings</translation>
    </message>
    <message>
        <source>设置中心内搜索</source>
        <translation type="vanished">Search in Settings</translation>
    </message>
    <message>
        <source>仅在设置中心打开时生效</source>
        <translation type="vanished">Active only while Settings is open</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ShortcutMap.cpp" line="51"/>
        <source>打开审计日志</source>
        <translation>Open audit log</translation>
    </message>
    <message>
        <source>显示/隐藏上下文面板</source>
        <translation type="vanished">Show/hide context panel</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ShortcutMap.cpp" line="54"/>
        <source>显示/隐藏右侧面板</source>
        <translation>Show/hide right panel</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ShortcutMap.cpp" line="57"/>
        <source>打开文件改动审阅</source>
        <translation>Open file change review</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ShortcutMap.cpp" line="60"/>
        <source>打开任务清单</source>
        <translation>Open task list</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ShortcutMap.cpp" line="63"/>
        <source>显示/隐藏侧边栏</source>
        <translation>Show/hide sidebar</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ShortcutMap.cpp" line="66"/>
        <source>结束当前对话</source>
        <translation>Close current conversation</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ShortcutMap.cpp" line="69"/>
        <source>切换到下一个对话</source>
        <translation>Switch to next conversation</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ShortcutMap.cpp" line="72"/>
        <source>切换到上一个对话</source>
        <translation>Switch to previous conversation</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ShortcutMap.cpp" line="75"/>
        <source>新建会话</source>
        <translation>New session</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ShortcutMap.cpp" line="78"/>
        <source>搜索会话</source>
        <translation>Search sessions</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ShortcutMap.cpp" line="81"/>
        <source>切换深色/浅色主题</source>
        <translation>Toggle dark/light theme</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ShortcutMap.cpp" line="84"/>
        <source>打开快捷键速查表</source>
        <translation>Open shortcut reference</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ShortcutMap.cpp" line="178"/>
        <source>未知的快捷键动作</source>
        <translation>Unknown shortcut action</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ShortcutMap.cpp" line="185"/>
        <source>无法识别的快捷键</source>
        <translation>Unrecognized shortcut</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ShortcutMap.cpp" line="200"/>
        <source>全局快捷键需要包含 Ctrl、Alt 或 Meta，或使用功能键</source>
        <translation>Global shortcuts need Ctrl, Alt, or Meta, or a function key</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ShortcutMap.cpp" line="213"/>
        <source>该快捷键已被「%1」使用</source>
        <translation>This shortcut is already used by &quot;%1&quot;</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ShortcutMap.cpp" line="227"/>
        <source>快捷键未能保存，请稍后重试</source>
        <translation>Could not save the shortcut. Please try again.</translation>
    </message>
</context>
<context>
    <name>NexusAI::AIAgent</name>
    <message>
        <location filename="../src/core/agent/AIAgent.cpp" line="856"/>
        <source>请先配置并启用模型，再发送消息。</source>
        <translation>Configure and enable a model before sending a message.</translation>
    </message>
    <message>
        <source>这个会话还没有可分支的历史记录。</source>
        <translation type="vanished">This conversation has no history to branch from yet.</translation>
    </message>
    <message>
        <source>找不到要分支的那条提问（可能已被上下文压缩或未载入），请重新打开该会话后再试。</source>
        <translation type="vanished">The question to branch from is no longer in this conversation (it may have been compressed out or not loaded). Reopen the session and try again.</translation>
    </message>
    <message>
        <source>那条提问没有可定位的记录编号，无法从这里分支。</source>
        <translation type="vanished">That question has no locatable record id, so it cannot be branched from.</translation>
    </message>
    <message>
        <source>无法保存这条新的提问，原对话保持不变。</source>
        <translation type="vanished">Could not save this new question. The conversation was left unchanged.</translation>
    </message>
    <message>
        <source>无法切换会话分支，原对话保持不变。</source>
        <translation type="vanished">Could not switch conversation branch. The conversation was left unchanged.</translation>
    </message>
    <message>
        <source>无法记录这一轮回答，原对话保持不变。</source>
        <translation type="vanished">Could not record this answer. The conversation was left unchanged.</translation>
    </message>
    <message>
        <source>无法切换到该版本，原对话保持不变。</source>
        <translation type="vanished">Could not switch to that version. The conversation was left unchanged.</translation>
    </message>
    <message>
        <source>无法删除该分支，请重试。</source>
        <translation type="vanished">Could not delete this branch. Please try again.</translation>
    </message>
    <message>
        <source>找不到要重置的那条消息（可能已被上下文压缩或未载入），请重新打开该会话后再试。</source>
        <translation type="vanished">The message to rewind to is no longer in this conversation (it may have been compressed out or not loaded). Reopen the session and try again.</translation>
    </message>
    <message>
        <source>该消息没有可定位的记录编号，无法安全地重置会话。</source>
        <translation type="vanished">This message has no locatable record id, so the conversation cannot be rewound safely.</translation>
    </message>
    <message>
        <source>时间线里找不到与该消息对应的记录，无法安全地重置会话。</source>
        <translation type="vanished">No timeline entry matches this message, so the conversation cannot be rewound safely.</translation>
    </message>
    <message>
        <location filename="../src/core/agent/AIAgent.cpp" line="1767"/>
        <source>无法更新会话记录，原消息已保留，请重试。</source>
        <translation>Could not update the conversation. Original messages were kept. Please retry.</translation>
    </message>
</context>
<context>
    <name>NexusAI::AgentViewModel</name>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1751"/>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1756"/>
        <source>保存项目规则失败：%1</source>
        <translation>Failed to save project rules: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="300"/>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="602"/>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2732"/>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2793"/>
        <source>新会话</source>
        <translation>New Session</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="727"/>
        <source>打开的对话过多，请先结束其中一些再打开新的。</source>
        <translation>Too many conversations are open. Close some before opening a new one.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="926"/>
        <source>请等待当前任务完成，或停止后切换模型。</source>
        <translation>Wait for the current task to finish, or stop it before switching models.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1315"/>
        <source>无法更新会话置顶状态，请重试。</source>
        <translation>Could not update the pinned state. Please retry.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1323"/>
        <source>无法更新会话归档状态，请重试。</source>
        <translation>Could not update the archive state. Please retry.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1327"/>
        <source>会话已归档，可在“包含已归档”中恢复。</source>
        <translation>Conversation archived. Enable “Include archived” to restore it.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1327"/>
        <source>会话已恢复。</source>
        <translation>Conversation restored.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1367"/>
        <source>导出失败：未连接持久化后端</source>
        <translation>Export failed: no persistent backend connected</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1372"/>
        <source>导出失败：不支持的格式“%1”（可选 md / json）</source>
        <translation>Export failed: unsupported format &quot;%1&quot; (md / json)</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1387"/>
        <source># NexusAI 会话导出

</source>
        <translation># NexusAI Session Export

</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1388"/>
        <source>- 会话 ID：%1
</source>
        <translation>- Session ID: %1
</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1389"/>
        <source>- 导出时间：%1
</source>
        <translation>- Exported at: %1
</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1390"/>
        <source>- 消息数：%1

---

</source>
        <translation>- Message count: %1

---

</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1394"/>
        <source>用户</source>
        <translation>User</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1395"/>
        <source>助手</source>
        <translation>Assistant</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1404"/>
        <source>---

## 过程摘要

</source>
        <translation>---

## Process Summary

</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1433"/>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1515"/>
        <source>导出失败：无法写入 %1</source>
        <translation>Export failed: cannot write %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1438"/>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1522"/>
        <source>导出失败：写入未完成 %1</source>
        <translation>Export failed: write incomplete %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1444"/>
        <source>已导出会话（%1 条消息）：%2</source>
        <translation>Session exported (%1 messages): %2</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1478"/>
        <source>导出失败：当前没有可导出的请求明细</source>
        <translation>Export failed: no request details to export</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1484"/>
        <source>导出失败：未指定文件路径</source>
        <translation>Export failed: no file path specified</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1490"/>
        <source>时间</source>
        <translation>Time</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1490"/>
        <source>模型</source>
        <translation>Model</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1490"/>
        <source>输入 Tokens</source>
        <translation>Input Tokens</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1490"/>
        <source>输出 Tokens</source>
        <translation>Output Tokens</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1491"/>
        <source>缓存命中 Tokens</source>
        <translation>Cache hit Tokens</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1491"/>
        <source>延迟(ms)</source>
        <translation>Latency (ms)</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1491"/>
        <source>费用(元)</source>
        <translation>Cost (CNY)</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1492"/>
        <source>会话 ID</source>
        <translation>Session ID</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1492"/>
        <source>请求 ID</source>
        <translation>Request ID</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1614"/>
        <source>无法读取文件：%1</source>
        <translation>Cannot read file: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1661"/>
        <source>PDF 已加密或需要密码，无法导入：%1</source>
        <translation>PDF is encrypted or password-protected, cannot import: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1664"/>
        <source>PDF 文件已损坏或格式无效，无法导入：%1</source>
        <translation>PDF file is corrupted or has an invalid format, cannot import: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1667"/>
        <source>未能从 PDF 中提取到文本（可能是扫描件，没有文本层）：%1</source>
        <translation>No text could be extracted from the PDF (it may be a scan without a text layer): %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1671"/>
        <source>无法读取 PDF 文件：%1</source>
        <translation>Cannot read PDF file: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1857"/>
        <source>仅支持 http/https URL：%1</source>
        <translation>Only http/https URLs are supported: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1881"/>
        <source>页面无可用正文：%1</source>
        <translation>The page has no usable text: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1894"/>
        <source>页面已重定向至 %1，请直接导入新地址。</source>
        <translation>The page redirected to %1; import the new address directly.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1898"/>
        <source>抓取失败：%1</source>
        <translation>Fetch failed: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2259"/>
        <source>数据库正被占用（可能有备份或其它任务在进行），请稍后重试</source>
        <translation>The database is busy (a backup or another task may be running). Try again later</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2270"/>
        <source>未指定备份文件</source>
        <translation>No backup file specified</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2279"/>
        <source>备份文件不存在</source>
        <translation>The backup file does not exist</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2283"/>
        <source>只能删除备份目录中的文件</source>
        <translation>Only files in the backup folder can be deleted</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2286"/>
        <source>删除失败，请检查文件是否被占用</source>
        <translation>Delete failed. Check whether the file is in use</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2776"/>
        <source>这个会话还没有可分叉的历史记录。</source>
        <translation>This conversation has no history to fork from yet.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2781"/>
        <source>这条回答还没有存进会话记录，稍等一下再试。</source>
        <translation>This answer is not saved to the conversation yet - try again in a moment.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2799"/>
        <source>%1（分支）</source>
        <translation>%1 (branch)</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2801"/>
        <source>%1（分支%2）</source>
        <translation>%1 (branch %2)</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2805"/>
        <source>无法创建分叉出来的会话，请重试。</source>
        <translation>Could not create the forked conversation. Please try again.</translation>
    </message>
    <message>
        <source>分叉会话已创建，但没能发出第一条消息。</source>
        <translation type="vanished">The forked conversation was created, but its first message could not be sent.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1636"/>
        <source>无法打开文件：%1</source>
        <translation>Cannot open file: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="1920"/>
        <source>知识库正忙（导入或重新嵌入进行中），请稍后再试</source>
        <translation>Knowledge base is busy (an import or re-embed is running); try again shortly</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2047"/>
        <source>文档内容已存在，已跳过：%1</source>
        <translation>Document content already exists, skipped: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2088"/>
        <source>未找到文档内容，无法重新嵌入：%1</source>
        <translation>Document content not found, cannot re-embed: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2108"/>
        <source>已重新嵌入 %1（%2 段）</source>
        <translation>Re-embedded %1 (%2 chunks)</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2110"/>
        <source>重新嵌入失败：%1</source>
        <translation>Re-embedding failed: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2114"/>
        <source>已导入 %1（%2 段）</source>
        <translation>Imported %1 (%2 chunks)</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2116"/>
        <source>导入失败（文件为空或无法分块）：%1</source>
        <translation>Import failed (file is empty or cannot be chunked): %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2118"/>
        <source>知识库总量已达上限（最多 %1 段），请先删除部分文档后再导入</source>
        <translation>Knowledge base has reached its limit (max %1 chunks); delete some documents before importing</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2121"/>
        <source>文件过大（超过单文档 %1 段上限）：%2</source>
        <translation>File too large (exceeds the per-document limit of %1 chunks): %2</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2184"/>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2218"/>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2236"/>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2249"/>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2257"/>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2298"/>
        <source>数据库未就绪</source>
        <translation>Database not ready</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2191"/>
        <source>备份失败，详见日志</source>
        <translation>Backup failed; see the log for details</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2303"/>
        <source>无法读取该备份文件：%1</source>
        <translation>Cannot read this backup file: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2305"/>
        <source>备份完整性检查未通过，已取消恢复</source>
        <translation>Backup failed the integrity check — restore cancelled</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2320"/>
        <source>恢复失败，原数据库已还原</source>
        <translation>Restore failed; the original database was kept</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2616"/>
        <source>文件过大（附件上限 %1 KB）：%2</source>
        <translation>File too large (attachment limit %1 KB): %2</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2621"/>
        <source>暂不支持二进制文件作为附件：%1</source>
        <translation>Binary files are not supported as attachments yet: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="2634"/>
        <source>已忽略 %1 个不支持的文件类型（仅支持图片与文本文件）</source>
        <translation>Ignored %1 file(s) of unsupported types (images and text files only)</translation>
    </message>
</context>
<context>
    <name>NexusAI::ChatSessionController</name>
    <message>
        <source>No model agent is configured. Configure a model and try again.</source>
        <translation type="vanished">No model agent is configured. Configure a model and try again.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ChatSessionController.cpp" line="302"/>
        <source>尚未配置模型，请先配置模型后重试。</source>
        <translation>No model is configured. Configure one and try again.</translation>
    </message>
</context>
<context>
    <name>NexusAI::CommitMessageDrafter</name>
    <message>
        <location filename="../src/viewmodels/CommitMessageDrafter.cpp" line="54"/>
        <source>起草超时（%1 秒）。</source>
        <translation>Drafting timed out (%1s).</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/CommitMessageDrafter.cpp" line="164"/>
        <source>已有一次起草在进行中，请等待或先取消。</source>
        <translation>A draft request is already running; wait or cancel it first.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/CommitMessageDrafter.cpp" line="168"/>
        <source>没有已暂存的改动，无法起草。</source>
        <translation>Nothing is staged, so there is nothing to draft.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/CommitMessageDrafter.cpp" line="186"/>
        <source>当前模型配置不完整（协议/地址/模型名），无法起草。</source>
        <translation>The current model profile is incomplete (protocol/base URL/model), so drafting is unavailable.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/CommitMessageDrafter.cpp" line="193"/>
        <source>已开启仅本地模式：不会把改动内容发送给云端模型。</source>
        <translation>Local-only mode is on: staged content is not sent to a cloud model.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/CommitMessageDrafter.cpp" line="199"/>
        <source>未知的模型接口协议：%1</source>
        <translation>Unknown model protocol: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/CommitMessageDrafter.cpp" line="232"/>
        <source>模型没有返回可用的提交说明。</source>
        <translation>The model returned no usable commit message.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/CommitMessageDrafter.cpp" line="254"/>
        <source>已取消起草。</source>
        <translation>Drafting cancelled.</translation>
    </message>
</context>
<context>
    <name>NexusAI::ConnectionTester</name>
    <message>
        <location filename="../src/viewmodels/ConnectionTester.cpp" line="63"/>
        <source>拉取超时（20 秒）</source>
        <translation>Fetch timed out (20 seconds)</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ConnectionTester.cpp" line="73"/>
        <location filename="../src/viewmodels/ConnectionTester.cpp" line="221"/>
        <source>未知的模型接口协议：%1</source>
        <translation>Unknown model API protocol: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ConnectionTester.cpp" line="91"/>
        <source>连接成功</source>
        <translation>Connection successful</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ConnectionTester.cpp" line="91"/>
        <source>连接成功：%1</source>
        <translation>Connection successful: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ConnectionTester.cpp" line="113"/>
        <source>连接超时（20 秒）</source>
        <translation>Connection timed out (20 s)</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ConnectionTester.cpp" line="154"/>
        <source>端点返回的不是 JSON 对象。</source>
        <translation>The endpoint did not return a JSON object.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ConnectionTester.cpp" line="210"/>
        <location filename="../src/viewmodels/ConnectionTester.cpp" line="282"/>
        <source>端点没有返回任何模型。</source>
        <translation>The endpoint returned no models.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ConnectionTester.cpp" line="226"/>
        <source>服务地址为空或无效，请先填写端点。</source>
        <translation>The service address is empty or invalid; fill in the endpoint first.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ConnectionTester.cpp" line="269"/>
        <source>请求失败：%1</source>
        <translation>Request failed: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ConnectionTester.cpp" line="274"/>
        <source>HTTP %1：%2</source>
        <translation>HTTP %1: %2</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/ConnectionTester.cpp" line="285"/>
        <source>拉取到 %1 个模型。</source>
        <translation>Fetched %1 models.</translation>
    </message>
</context>
<context>
    <name>NexusAI::EditBuffer</name>
    <message>
        <source>值必须是整数</source>
        <translation type="vanished">Value must be an integer</translation>
    </message>
    <message>
        <source>值必须在 %1-%2 之间</source>
        <translation type="vanished">Value must be between %1-%2</translation>
    </message>
    <message>
        <source>值必须是以下之一: </source>
        <translation type="vanished">Value must be one of: </translation>
    </message>
    <message>
        <source>此字段为必填</source>
        <translation type="vanished">This field is required</translation>
    </message>
    <message>
        <source>长度不能超过 %1 个字符</source>
        <translation type="vanished">Length cannot exceed %1 characters</translation>
    </message>
</context>
<context>
    <name>NexusAI::McpConfigTransfer</name>
    <message>
        <location filename="../src/core/mcp/McpConfigTransfer.cpp" line="44"/>
        <source>缺少服务器名称</source>
        <translation>Server name is missing</translation>
    </message>
    <message>
        <location filename="../src/core/mcp/McpConfigTransfer.cpp" line="47"/>
        <source>传输方式必须是 stdio 或 http</source>
        <translation>Transport must be stdio or http</translation>
    </message>
    <message>
        <location filename="../src/core/mcp/McpConfigTransfer.cpp" line="50"/>
        <source>stdio 服务器缺少启动命令</source>
        <translation>stdio server is missing its command</translation>
    </message>
    <message>
        <location filename="../src/core/mcp/McpConfigTransfer.cpp" line="52"/>
        <source>http 服务器缺少地址</source>
        <translation>http server is missing its URL</translation>
    </message>
    <message>
        <location filename="../src/core/mcp/McpConfigTransfer.cpp" line="111"/>
        <source>请选择保存位置</source>
        <translation>Choose where to save</translation>
    </message>
    <message>
        <location filename="../src/core/mcp/McpConfigTransfer.cpp" line="116"/>
        <source>无法写入文件：%1</source>
        <translation>Cannot write file: %1</translation>
    </message>
    <message>
        <location filename="../src/core/mcp/McpConfigTransfer.cpp" line="121"/>
        <source>保存失败：%1</source>
        <translation>Save failed: %1</translation>
    </message>
    <message>
        <location filename="../src/core/mcp/McpConfigTransfer.cpp" line="131"/>
        <source>请选择配置文件</source>
        <translation>Choose a config file</translation>
    </message>
    <message>
        <location filename="../src/core/mcp/McpConfigTransfer.cpp" line="136"/>
        <source>文件不存在：%1</source>
        <translation>File not found: %1</translation>
    </message>
    <message>
        <location filename="../src/core/mcp/McpConfigTransfer.cpp" line="140"/>
        <source>无法读取文件：%1</source>
        <translation>Cannot read file: %1</translation>
    </message>
    <message>
        <location filename="../src/core/mcp/McpConfigTransfer.cpp" line="146"/>
        <source>不是有效的 JSON：%1</source>
        <translation>Not valid JSON: %1</translation>
    </message>
    <message>
        <location filename="../src/core/mcp/McpConfigTransfer.cpp" line="150"/>
        <location filename="../src/core/mcp/McpConfigTransfer.cpp" line="156"/>
        <source>不是有效的 MCP 服务器配置文件</source>
        <translation>Not a valid MCP server config file</translation>
    </message>
    <message>
        <location filename="../src/core/mcp/McpConfigTransfer.cpp" line="161"/>
        <source>文件格式不受支持：%1</source>
        <translation>Unsupported file format: %1</translation>
    </message>
</context>
<context>
    <name>NexusAI::McpManager</name>
    <message>
        <location filename="../src/viewmodels/McpManager.cpp" line="227"/>
        <location filename="../src/viewmodels/McpManager.cpp" line="284"/>
        <location filename="../src/viewmodels/McpManager.cpp" line="291"/>
        <source>已存在名为「%1」的服务器</source>
        <translation>A server named &quot;%1&quot; already exists</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/McpManager.cpp" line="240"/>
        <location filename="../src/viewmodels/McpManager.cpp" line="308"/>
        <location filename="../src/viewmodels/McpManager.cpp" line="367"/>
        <location filename="../src/viewmodels/McpManager.cpp" line="386"/>
        <location filename="../src/viewmodels/McpManager.cpp" line="530"/>
        <source>保存 MCP 服务器配置失败</source>
        <translation>Failed to save MCP server configuration</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/McpManager.cpp" line="447"/>
        <source>还没有可导出的 MCP 服务器</source>
        <translation>There are no MCP servers to export</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/McpManager.cpp" line="518"/>
        <source>文件里没有可导入的服务器</source>
        <translation>The file has no importable servers</translation>
    </message>
</context>
<context>
    <name>NexusAI::PreferenceStore</name>
    <message>
        <location filename="../src/viewmodels/PreferenceStore.cpp" line="84"/>
        <source>无法保存未登记的设置项：%1</source>
        <translation>Cannot save an unregistered setting: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PreferenceStore.cpp" line="99"/>
        <source>设置未能保存。请检查存储权限或磁盘空间后重试。</source>
        <translation>Settings could not be saved. Check storage permissions or disk space and retry.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PreferenceStore.cpp" line="121"/>
        <source>无法重置未登记的设置项：%1</source>
        <translation>Cannot reset an unregistered setting: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PreferenceStore.cpp" line="130"/>
        <source>设置未能重置。请检查存储权限或磁盘空间后重试。</source>
        <translation>Settings could not be reset. Check storage permissions or disk space and retry.</translation>
    </message>
</context>
<context>
    <name>NexusAI::PresetPackTransfer</name>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="86"/>
        <source>无法读取预设包：%1</source>
        <translation>Could not read the preset pack: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="87"/>
        <source>预设包超过 2 MB 限制</source>
        <translation>Preset pack exceeds the 2 MB limit</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="93"/>
        <source>预设包不是有效的 JSON 对象</source>
        <translation>Preset pack is not a valid JSON object</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="98"/>
        <source>不支持的预设包格式或版本</source>
        <translation>Unsupported preset pack format or version</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="101"/>
        <source>预设包缺少预设或指令列表</source>
        <translation>Preset pack is missing the presets or commands list</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="106"/>
        <source>预设条目格式错误</source>
        <translation>Preset entry has an invalid format</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="110"/>
        <source>预设条目缺少名称或内容</source>
        <translation>Preset entry is missing a name or content</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="112"/>
        <source>预设包包含重名预设：%1</source>
        <translation>Preset pack contains a duplicate preset name: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="114"/>
        <source>预设数量超过 %1 条限制</source>
        <translation>Preset count exceeds the %1 entry limit</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="123"/>
        <source>指令条目格式错误</source>
        <translation>Command entry has an invalid format</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="129"/>
        <source>指令条目缺少指令词或模板</source>
        <translation>Command entry is missing a command word or template</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="131"/>
        <source>预设包包含重复指令：/%1</source>
        <translation>Preset pack contains a duplicate command: /%1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="133"/>
        <source>指令数量超过 %1 条限制</source>
        <translation>Command count exceeds the %1 entry limit</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="144"/>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="147"/>
        <source>禁用工具列表格式错误</source>
        <translation>Disabled tools list has an invalid format</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="151"/>
        <source>禁用工具数量超过 %1 条限制</source>
        <translation>Disabled tool count exceeds the %1 entry limit</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="158"/>
        <source>默认权限模式格式错误</source>
        <translation>Default permission mode has an invalid format</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="163"/>
        <source>预设包包含未知的默认权限模式：%1</source>
        <translation>Preset pack contains an unknown default permission mode: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="211"/>
        <source>无法创建预设包文件：%1</source>
        <translation>Could not create the preset pack file: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="216"/>
        <source>预设包写入失败：%1</source>
        <translation>Failed to write the preset pack: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="286"/>
        <source>预设包不含模型配置、API Key 或 MCP 凭据。</source>
        <translation>The preset pack contains no model configuration, API keys, or MCP credentials.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="287"/>
        <source>导入会同时应用包内的禁用工具与默认权限模式，并立即生效（无需重启）。</source>
        <translation>Import also applies the pack&apos;s disabled tools and default permission mode, effective immediately (no restart).</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="288"/>
        <source>会话模板、按项目覆盖与本机凭据不包含在预设包内。</source>
        <translation>Session templates, per-project overrides, and local credentials are not part of a preset pack.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="290"/>
        <source>有 %1 个工具名未在本机注册，导入时会被忽略。</source>
        <translation>%1 tool names are not registered on this machine and will be ignored on import.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="293"/>
        <source>包内禁用工具均未在本机注册，工具启用状态保持不变。</source>
        <translation>None of the pack&apos;s disabled tools are registered on this machine; tool enablement is left unchanged.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="322"/>
        <source>未知的冲突处理策略</source>
        <translation>Unknown conflict handling policy</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="424"/>
        <source>预设包中的预设或指令无效，导入已取消</source>
        <translation>The preset pack contains invalid presets or commands; import cancelled</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="439"/>
        <source>工具启用状态写入失败，导入已回滚</source>
        <translation>Failed to save tool enablement; import rolled back</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/PresetPackTransfer.cpp" line="449"/>
        <source>默认权限模式写入失败，导入已回滚</source>
        <translation>Failed to save the default permission mode; import rolled back</translation>
    </message>
</context>
<context>
    <name>NexusAI::SettingsBackup</name>
    <message>
        <source>无法读取当前设置</source>
        <translation type="vanished">Unable to read current settings</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="229"/>
        <source>模型配置已损坏，无法安全导出</source>
        <translation>Model profiles are corrupted and cannot be exported safely</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="234"/>
        <source>模型配置包含无效条目</source>
        <translation>Model profiles contain an invalid entry</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="249"/>
        <source>无法读取 MCP 服务器配置</source>
        <translation>Unable to read MCP server configuration</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="256"/>
        <source>MCP 服务器配置已损坏</source>
        <translation>MCP server configuration is corrupted</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="261"/>
        <source>MCP 服务器配置包含无效条目</source>
        <translation>MCP server configuration contains an invalid entry</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="300"/>
        <source>无法创建导出文件：%1</source>
        <translation>Unable to create export file: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="305"/>
        <source>导出文件写入失败：%1</source>
        <translation>Failed to write export file: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="317"/>
        <source>未指定诊断包保存位置</source>
        <translation>No destination folder for the diagnostics bundle</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="322"/>
        <source>保存位置不存在：%1</source>
        <translation>Destination folder does not exist: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="333"/>
        <source>无法创建诊断包目录：%1</source>
        <translation>Could not create the diagnostics folder: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="346"/>
        <source>写入 settings.json 失败：%1</source>
        <translation>Failed to write settings.json: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="397"/>
        <source>写入 environment.txt 失败：%1</source>
        <translation>Failed to write environment.txt: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="410"/>
        <source>无法读取备份文件：%1</source>
        <translation>Unable to read backup file: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="411"/>
        <source>备份文件超过 2 MB 限制</source>
        <translation>Backup file exceeds the 2 MB limit</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="415"/>
        <source>备份文件不是有效的 JSON 对象</source>
        <translation>Backup file is not a valid JSON object</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="419"/>
        <source>不支持的设置备份格式或版本</source>
        <translation>Unsupported settings backup format or version</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="421"/>
        <source>设置版本不匹配，请使用兼容版本的应用恢复</source>
        <translation>Settings version mismatch; restore with a compatible version of the app</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="425"/>
        <source>备份缺少设置、模型或 MCP 数据段</source>
        <translation>Backup is missing settings, model profiles, or MCP server data</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="431"/>
        <source>备份包含不允许恢复或类型错误的设置项：%1</source>
        <translation>Backup contains a setting that cannot be restored or has the wrong type: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="438"/>
        <source>模型配置格式错误</source>
        <translation>Invalid model profile format</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="440"/>
        <source>备份包含密钥字段，拒绝恢复</source>
        <translation>Backup contains a secret field; restore refused</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="447"/>
        <source>模型配置 ID 或协议无效</source>
        <translation>Invalid model profile ID or protocol</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="454"/>
        <source>活动模型 ID 未出现在模型列表中</source>
        <translation>Active model ID is not in the model profile list</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="459"/>
        <source>MCP 配置格式错误</source>
        <translation>Invalid MCP configuration format</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="462"/>
        <source>备份包含 MCP 凭据字段，拒绝恢复</source>
        <translation>Backup contains MCP credential fields; restore refused</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="468"/>
        <source>MCP 服务器名称或传输方式无效</source>
        <translation>Invalid MCP server name or transport</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="504"/>
        <source>未包含</source>
        <translation>Not included</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="522"/>
        <source>应用设置（%1 项）</source>
        <translation>App settings (%1 items)</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="523"/>
        <source>模型配置（%1 条）</source>
        <translation>Model profiles (%1)</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="524"/>
        <source>MCP 服务器（%1 条，恢复后保持禁用）</source>
        <translation>MCP servers (%1, remain disabled after restore)</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="526"/>
        <source>安全权限、工具启用状态、开机自启及运行时布局不会恢复。</source>
        <translation>Security permissions, tool enablement, launch at startup, and runtime layout will not be restored.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="560"/>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="612"/>
        <source>无法读取当前设置，恢复已取消：%1</source>
        <translation>Could not read current settings. Restore cancelled: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="631"/>
        <source>恢复失败且设置回滚未完成，请检查当前设置：%1</source>
        <translation>Restore failed and rollback is incomplete. Check your current settings: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="525"/>
        <source>相同模型端点和 MCP 服务的本机凭据将保留；其他配置不会从备份恢复凭据。</source>
        <translation>Local credentials for matching model endpoints and MCP servers are retained; other configurations do not receive credentials from the backup.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="201"/>
        <source>无法读取当前设置：%1</source>
        <translation>Could not read current settings: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="527"/>
        <source>MCP 命令参数、服务地址或自定义文本可能包含自行填写的敏感信息，请检查备份文件。</source>
        <translation>MCP command arguments, server addresses, or custom text may contain sensitive information you entered. Review the backup file.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="528"/>
        <source>恢复后需要重启应用；会话和知识库数据库不在此备份内。</source>
        <translation>Restart the app after restoring; chats and knowledge base databases are not included in this backup.</translation>
    </message>
    <message>
        <source>无法读取当前设置，恢复已取消</source>
        <translation type="vanished">Unable to read current settings; restore canceled</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="570"/>
        <source>当前模型配置已损坏，恢复已取消</source>
        <translation>Current model profiles are corrupted; restore canceled</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="618"/>
        <source>设置写入失败，恢复已取消：%1</source>
        <translation>Could not write settings. Restore cancelled: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsBackup.cpp" line="630"/>
        <source>恢复失败，原有设置已保留</source>
        <translation>Restore failed; existing settings were preserved</translation>
    </message>
    <message>
        <source>恢复失败且回滚未完成，请检查当前设置</source>
        <translation type="vanished">Restore failed and rollback was incomplete; check your current settings</translation>
    </message>
</context>
<context>
    <name>NexusAI::SettingsService</name>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="269"/>
        <source>设置未能保存，请检查存储权限或磁盘空间后重试。</source>
        <translation>Settings could not be saved. Check storage permissions or disk space and retry.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="554"/>
        <source>API 密钥加密失败，模型配置未保存。</source>
        <translation>API key encryption failed. Model configuration was not saved.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="575"/>
        <source>模型配置未能保存。请检查设置存储权限或磁盘空间后重试。</source>
        <translation>Model configuration could not be saved. Check storage permissions or disk space and retry.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="871"/>
        <source>自定义网关</source>
        <translation>Custom Gateway</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="876"/>
        <source>新配置</source>
        <translation>New Profile</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="1030"/>
        <source>%1 副本</source>
        <translation>%1 copy</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="1173"/>
        <source>OpenAI 兼容</source>
        <translation>OpenAI Compatible</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="1174"/>
        <source>Ollama 本地</source>
        <translation>Ollama (Local)</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="1177"/>
        <source>Gemini</source>
        <translation>Gemini</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="1178"/>
        <source>Claude</source>
        <translation>Claude</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="1348"/>
        <source>向量模型 API 密钥加密失败，未保存更改。</source>
        <translation>Embedding API key encryption failed. Changes were not saved.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="1475"/>
        <source>搜索 API 密钥加密失败，未保存更改。</source>
        <translation>Search API key encryption failed. Changes were not saved.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="1504"/>
        <source>测试失败：未知的搜索服务 %1。</source>
        <translation>Test failed: unknown search provider %1.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="1514"/>
        <source>测试失败：请先填写 %1 的 API Key。</source>
        <translation>Test failed: enter the API key for %1 first.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="1522"/>
        <source>正在测试搜索连接…</source>
        <translation>Testing search connection…</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="1542"/>
        <source>%1 连接失败：%2</source>
        <translation>%1 connection failed: %2</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="1546"/>
        <source>%1 连接正常（返回 %2 条结果）。</source>
        <translation>%1 connection is working (%2 results returned).</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="1638"/>
        <source>代理认证信息加密失败，未保存更改。</source>
        <translation>Failed to encrypt proxy credentials; changes were not saved.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="1703"/>
        <source>代理测试失败：测试地址无效。</source>
        <translation>Proxy test failed: invalid test URL.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="1709"/>
        <source>代理测试失败：请先填写有效的代理地址和端口。</source>
        <translation>Proxy test failed: enter a valid proxy host and port first.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="1734"/>
        <source>正在测试代理…</source>
        <translation>Testing proxy…</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="1759"/>
        <source>代理测试失败：%1</source>
        <translation>Proxy test failed: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="1765"/>
        <source>代理测试失败：未收到任何 HTTP 响应。</source>
        <translation>Proxy test failed: no HTTP response received.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="1768"/>
        <source>代理连接正常（HTTP %1）。</source>
        <translation>Proxy connection is working (HTTP %1).</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="2170"/>
        <source>无法更新 Windows 开机自启动设置。</source>
        <translation>Could not update Windows startup settings.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="611"/>
        <source>技术评审</source>
        <translation>Rigorous code reviewer</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="612"/>
        <source>你是一位严谨的技术评审。审查代码时只关注真实问题：正确性缺陷、边界条件、安全隐患、性能陷阱与可维护性风险。不写客套与表扬，按严重程度排序输出发现，每条给出位置与具体修改建议。没有问题时明确说&quot;未发现问题&quot;，不硬凑。</source>
        <translation>You are a rigorous code reviewer. Focus only on real problems: correctness defects, edge cases, security risks, performance traps and maintainability issues. No pleasantries or praise - report findings ordered by severity, each with a location and a concrete fix. When there is nothing to find, say so plainly instead of inventing issues.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="615"/>
        <source>资深架构师</source>
        <translation>Senior architect</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="616"/>
        <source>你是一位资深软件架构师。面对需求先明确约束与取舍，再给方案：优先给出最小可行的解决路径，说明所选方案的代价与放弃的替代项。复杂设计先讲清结构再动手，始终警惕过度设计与过早优化。</source>
        <translation>You are a senior software architect. Clarify constraints and trade-offs before proposing anything: prefer the minimal viable path, and state what the chosen approach costs and which alternatives were rejected. For complex designs explain the structure before writing code; stay alert to over-engineering and premature optimization.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="619"/>
        <source>耐心导师</source>
        <translation>Patient mentor</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="620"/>
        <source>你是一位耐心的技术导师，面向初学者解释问题。每次只引入一个新概念，用类比帮助理解，先讲&quot;为什么&quot;再讲&quot;怎么做&quot;。学生答错时不直接给答案，先给提示；确认理解后再继续下一步。</source>
        <translation>You are a patient technical mentor teaching beginners. Introduce one new concept at a time, use analogies, and always explain the &quot;why&quot; before the &quot;how&quot;. When the learner gets it wrong, offer a hint instead of the answer; confirm understanding before moving on.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="623"/>
        <source>简洁模式</source>
        <translation>Concise mode</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="624"/>
        <source>用尽可能少的篇幅回答。优先给结论和关键依据，不写铺垫、总结与客套；能一句话说清的不用两句。需要展开时先给一句话版本，被追问再展开。</source>
        <translation>Answer in as few words as possible. Lead with the conclusion and the key evidence - no preamble, no summary, no pleasantries; if one sentence suffices, never use two. When detail is needed, give the one-line version first and expand only when asked.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="639"/>
        <source>审查代码或 diff</source>
        <translation>Review code or a diff</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="640"/>
        <source>请审查以下代码，重点检查正确性问题、边界条件与潜在 bug，按严重程度排序；只给关键修改建议，不要重写全部：

{text}</source>
        <translation>Review the following code, focusing on correctness issues, edge cases and potential bugs, ordered by severity; give only key fix suggestions, do not rewrite everything:

{text}</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="642"/>
        <source>解释代码</source>
        <translation>Explain code</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="643"/>
        <source>请详细解释下面代码的作用与执行逻辑，包括关键分支、边界处理和可能的坑：

{text}</source>
        <translation>Explain in detail what the following code does and how it executes, including key branches, boundary handling and possible pitfalls:

{text}</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="644"/>
        <source>翻译成英文</source>
        <translation>Translate to English</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="645"/>
        <source>将下面的内容翻译成英文，保持原意与语气；技术术语、专有名词与代码保持原文：

{text}</source>
        <translation>Translate the following content into English, preserving meaning and tone; keep technical terms, proper nouns and code as-is:

{text}</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="646"/>
        <source>生成提交信息</source>
        <translation>Draft a commit message</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="647"/>
        <source>为以下代码改动生成一条符合 Conventional Commits 规范的中文提交信息（格式：type(scope): subject，正文用要点列表说明动机与改动），只输出提交信息本身：

{text}</source>
        <translation>Draft a Conventional Commits commit message in Chinese for the following changes (format: type(scope): subject, body as a bullet list covering motivation and the change); output only the commit message itself:

{text}</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="664"/>
        <source>代码审查</source>
        <translation>Code review</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="666"/>
        <source>请审查这个项目的近期改动：先看当前 git 状态与未提交的变更，再指出正确性、安全性与可维护性问题，按严重程度排序。</source>
        <translation>Review this project&apos;s recent changes: start with the current git status and uncommitted changes, then report correctness, security and maintainability issues ordered by severity.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="668"/>
        <source>项目探索</source>
        <translation>Explore project</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="670"/>
        <source>我是第一次接触这个项目。请带我了解它：先浏览目录结构，然后介绍核心模块的职责、它们之间的数据流，以及从哪里开始读代码最合适。</source>
        <translation>I am new to this project. Walk me through it: first the directory layout, then what each core module is responsible for, how data flows between them, and where I should start reading.</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="672"/>
        <source>快速润色</source>
        <translation>Quick polish</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsService.cpp" line="674"/>
        <source>请润色我接下来粘贴的文字：保持原意不变，让它更清晰、专业、简洁，并保留我的语气。</source>
        <translation>Polish the text I am about to paste: keep the meaning unchanged while making it clearer, more professional and more concise, preserving my voice.</translation>
    </message>
</context>
<context>
    <name>OnboardingDialog</name>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="48"/>
        <source>OpenAI 兼容</source>
        <translation>OpenAI-compatible</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="49"/>
        <source>Claude</source>
        <translation>Claude</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="50"/>
        <source>Gemini</source>
        <translation>Gemini</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="51"/>
        <source>Ollama 本地</source>
        <translation>Ollama (local)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="52"/>
        <source>自定义网关</source>
        <translation>Custom gateway</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="55"/>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="60"/>
        <source>跟随系统</source>
        <translation>Follow system</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="56"/>
        <source>中文</source>
        <translation>中文</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="57"/>
        <source>English</source>
        <translation>English</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="61"/>
        <source>浅色</source>
        <translation>Light</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="62"/>
        <source>深色</source>
        <translation>Dark</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="132"/>
        <source>无法创建模型配置，请检查设置写入权限后重试。</source>
        <translation>Could not create the model profile. Check that settings are writable and try again.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="199"/>
        <source>正在测试连接…</source>
        <translation>Testing connection…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="235"/>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="244"/>
        <source>无法保存向导状态，请稍后重试。</source>
        <translation>Could not save the setup state. Please try again.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="255"/>
        <source>导入失败。</source>
        <translation>Import failed.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="260"/>
        <source>设置已导入，重启应用后生效。</source>
        <translation>Settings imported. Restart the app to apply them.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="261"/>
        <source>设置已导入。</source>
        <translation>Settings imported.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="266"/>
        <source>选择设置备份</source>
        <translation>Choose a settings backup</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="268"/>
        <source>JSON 文件 (*.json)</source>
        <translation>JSON files (*.json)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="277"/>
        <source>无法读取此备份文件。</source>
        <translation>This backup file could not be read.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="300"/>
        <source>首次启动向导</source>
        <translation>First-run setup</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="311"/>
        <source>欢迎使用 NexusAI</source>
        <translation>Welcome to NexusAI</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="312"/>
        <source>配置模型服务</source>
        <translation>Set up a model service</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="313"/>
        <source>导入旧设置</source>
        <translation>Import old settings</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="325"/>
        <source>先选择界面语言与主题，随时可以在设置中心修改。</source>
        <translation>Pick the interface language and theme. You can change both later in Settings.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="327"/>
        <source>填写模型服务地址、密钥与模型名。密钥只保存在本机并加密，不会进入设置备份。</source>
        <translation>Enter the endpoint, API key, and model name. The key is stored locally and encrypted; it is never included in settings backups.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="328"/>
        <source>如果你在另一台设备或旧版本里导出过设置备份，可以在这里恢复；也可以直接跳过。</source>
        <translation>If you exported a settings backup on another device or an older version, restore it here. You can also skip this step.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="338"/>
        <source>第 %1 步，共 3 步</source>
        <translation>Step %1 of 3</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="367"/>
        <source>界面语言</source>
        <translation>Interface language</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="397"/>
        <source>主题</source>
        <translation>Theme</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="433"/>
        <source>服务来源</source>
        <translation>Service source</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="458"/>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="468"/>
        <source>服务地址</source>
        <translation>Endpoint</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="469"/>
        <source>https://api.example.com/v1</source>
        <translation>https://api.example.com/v1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="482"/>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="492"/>
        <source>API Key</source>
        <translation>API Key</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="494"/>
        <source>留空则不修改；本地服务可以留空</source>
        <translation>Leave blank to keep the current key; local services can be blank</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="507"/>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="517"/>
        <source>模型名</source>
        <translation>Model name</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="518"/>
        <source>例如 gpt-4o-mini</source>
        <translation>e.g. gpt-4o-mini</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="536"/>
        <source>测试连接</source>
        <translation>Test connection</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="559"/>
        <source>选择设置备份…</source>
        <translation>Choose a settings backup…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="601"/>
        <source>导入设置</source>
        <translation>Import settings</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="626"/>
        <source>跳过向导</source>
        <translation>Skip setup</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="635"/>
        <source>上一步</source>
        <translation>Back</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="642"/>
        <source>下一步</source>
        <translation>Next</translation>
    </message>
    <message>
        <location filename="../src/ui/components/OnboardingDialog.qml" line="650"/>
        <source>完成</source>
        <translation>Finish</translation>
    </message>
</context>
<context>
    <name>ParamSelectRow</name>
    <message>
        <location filename="../src/ui/components/ParamSelectRow.qml" line="28"/>
        <source>自动</source>
        <translation>Auto</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ParamSelectRow.qml" line="41"/>
        <source>自定义…</source>
        <translation>Custom...</translation>
    </message>
</context>
<context>
    <name>PermissionModeSelector</name>
    <message>
        <location filename="../src/ui/components/PermissionModeSelector.qml" line="30"/>
        <location filename="../src/ui/components/PermissionModeSelector.qml" line="150"/>
        <source>完全访问</source>
        <translation>Full Access</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PermissionModeSelector.qml" line="31"/>
        <location filename="../src/ui/components/PermissionModeSelector.qml" line="145"/>
        <source>自动编辑</source>
        <translation>Auto Edit</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PermissionModeSelector.qml" line="32"/>
        <source>只读</source>
        <translation>Read-only</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PermissionModeSelector.qml" line="33"/>
        <location filename="../src/ui/components/PermissionModeSelector.qml" line="140"/>
        <source>变更确认</source>
        <translation>Confirm Changes</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PermissionModeSelector.qml" line="39"/>
        <source>会话权限模式</source>
        <translation>Session Permission Mode</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PermissionModeSelector.qml" line="135"/>
        <source>只读（计划模式）</source>
        <translation>Read-only (plan mode)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PermissionModeSelector.qml" line="136"/>
        <source>仅分析与检索；写入文件、执行命令、提交与 MCP 工具将被拒绝</source>
        <translation>Analyze and retrieve only; file writes, commands, commits and MCP tools are refused</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PermissionModeSelector.qml" line="141"/>
        <source>敏感工具执行前逐次询问（默认）</source>
        <translation>Ask before every sensitive tool call (default)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PermissionModeSelector.qml" line="146"/>
        <source>自动执行文件编辑与搜索抓取；命令与 MCP 工具仍询问</source>
        <translation>Auto-run file edits, web search and fetch; shell and MCP tools still ask</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PermissionModeSelector.qml" line="151"/>
        <source>所有工具自动执行，不再询问</source>
        <translation>Run every tool automatically without asking</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PermissionModeSelector.qml" line="269"/>
        <source>启用完全访问？</source>
        <translation>Enable Full Access?</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PermissionModeSelector.qml" line="277"/>
        <source>本会话中所有工具（包括 shell 命令、文件写入、MCP 工具）</source>
        <translation>In this session every tool (including shell commands, file writes, MCP tools)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PermissionModeSelector.qml" line="278"/>
        <source>都将不经逐次确认自动执行，仅在会话内生效，重启后回落到默认模式。确定继续吗？</source>
        <translation>will run automatically without per-call approval. This lasts for the session only and resets to the default after restart. Continue?</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PermissionModeSelector.qml" line="290"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PermissionModeSelector.qml" line="294"/>
        <source>启用完全访问</source>
        <translation>Enable Full Access</translation>
    </message>
</context>
<context>
    <name>PreferenceControl</name>
    <message>
        <location filename="../src/ui/components/PreferenceControl.qml" line="76"/>
        <source>恢复默认值</source>
        <translation>Restore default</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PreferenceControl.qml" line="80"/>
        <source>将「%1」恢复为默认值</source>
        <translation>Restore “%1” to its default value</translation>
    </message>
</context>
<context>
    <name>ProcessCard</name>
    <message>
        <location filename="../src/ui/components/ProcessCard.qml" line="72"/>
        <source>新</source>
        <translation>New</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProcessCard.qml" line="73"/>
        <source>增</source>
        <translation>Add</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProcessCard.qml" line="74"/>
        <source>删</source>
        <translation>Del</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProcessCard.qml" line="75"/>
        <source>改</source>
        <translation>Mod</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProcessCard.qml" line="76"/>
        <source>移</source>
        <translation>Move</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProcessCard.qml" line="93"/>
        <source>思考</source>
        <translation>Thinking</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProcessCard.qml" line="94"/>
        <source>工具调用</source>
        <translation>Tool call</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProcessCard.qml" line="95"/>
        <source>工具结果</source>
        <translation>Tool result</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProcessCard.qml" line="96"/>
        <source>上下文</source>
        <translation>Context</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProcessCard.qml" line="101"/>
        <source>进行中</source>
        <translation>In progress</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProcessCard.qml" line="102"/>
        <source>失败</source>
        <translation>Failed</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProcessCard.qml" line="103"/>
        <source>已取消</source>
        <translation>Cancelled</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProcessCard.qml" line="104"/>
        <source>待确认</source>
        <translation>Awaiting confirmation</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProcessCard.qml" line="239"/>
        <source>已折叠 · %1 字符</source>
        <translation>Collapsed · %1 chars</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProcessCard.qml" line="316"/>
        <source>变更预览:</source>
        <translation>Change preview:</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProcessCard.qml" line="336"/>
        <source>拒绝</source>
        <translation>Reject</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProcessCard.qml" line="344"/>
        <source>允许</source>
        <translation>Allow</translation>
    </message>
</context>
<context>
    <name>ProjectFilesPanel</name>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="275"/>
        <source>无法打开该文件（可能已被删除或没有关联程序）。</source>
        <translation>Could not open the file (it may have been deleted, or no program is associated with it).</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="386"/>
        <source>按名称过滤项目文件</source>
        <translation>Filter project files by name</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="392"/>
        <source>过滤文件…</source>
        <translation>Filter files…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="407"/>
        <source>折叠全部目录</source>
        <translation>Collapse all folders</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="418"/>
        <source>重新读取项目文件</source>
        <translation>Reload project files</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="549"/>
        <source>整个项目没有匹配“%1”的文件。</source>
        <translation>No file in the whole project matches &quot;%1&quot;.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="551"/>
        <source>没有匹配的文件。</source>
        <translation>No matching files.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="552"/>
        <source>该目录为空。</source>
        <translation>This folder is empty.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="618"/>
        <source>语法高亮语言：%1</source>
        <translation>Syntax highlighting language: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="626"/>
        <source>行号</source>
        <translation>Lines</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="632"/>
        <source>已显示行号，点击隐藏</source>
        <translation>Line numbers are shown; click to hide</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="633"/>
        <source>显示行号</source>
        <translation>Show line numbers</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="644"/>
        <source>换行</source>
        <translation>Wrap</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="653"/>
        <source>已开启自动换行，点击改为横向滚动</source>
        <translation>Soft wrap is on; click to scroll horizontally instead</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="654"/>
        <source>开启自动换行，长行折行显示</source>
        <translation>Turn on soft wrap so long lines fold</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="668"/>
        <source>在文件树中定位该文件</source>
        <translation>Show this file in the tree</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="672"/>
        <source>未能在文件树中找到该文件（可能已被删除或移出项目）。</source>
        <translation>Could not find that file in the tree (it may have been deleted or moved out of the project).</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="686"/>
        <source>复制文件绝对路径</source>
        <translation>Copy the absolute file path</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="702"/>
        <source>用系统默认程序打开</source>
        <translation>Open with the default application</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="713"/>
        <source>收起预览</source>
        <translation>Collapse preview</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="727"/>
        <source>二进制文件，无法预览。</source>
        <translation>Binary file; no preview available.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="729"/>
        <source>文件已不存在，可能已被删除或改名。</source>
        <translation>The file no longer exists; it may have been deleted or renamed.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="731"/>
        <source>无法读取该文件（权限不足或已被占用）。</source>
        <translation>Could not read the file (permission denied or file in use).</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="733"/>
        <source>该路径不在项目内，已拒绝读取。</source>
        <translation>That path is outside the project; reading was refused.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="735"/>
        <source>读取失败：%1</source>
        <translation>Read failed: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="737"/>
        <source>文件较大（%1 字节），仅显示前 512 KB。</source>
        <translation>Large file (%1 bytes); only the first 512 KB are shown.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="743"/>
        <source>文件超过 %1 KB，已关闭语法高亮。</source>
        <translation>File exceeds %1 KB; syntax highlighting is off.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="863"/>
        <source>文件内容预览</source>
        <translation>File content preview</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="914"/>
        <source>当前会话未关联项目；在项目中打开的会话才能浏览项目文件。</source>
        <translation>This session is not tied to a project; project files can be browsed in sessions opened from a project.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ProjectFilesPanel.qml" line="922"/>
        <source>项目文件</source>
        <translation>Project files</translation>
    </message>
</context>
<context>
    <name>ProjectRulesPage</name>
    <message>
        <location filename="../src/ui/pages/ProjectRulesPage.qml" line="144"/>
        <source>项目规则</source>
        <translation>Project rules</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ProjectRulesPage.qml" line="145"/>
        <source>每个项目一份 AGENTS.md，注入该项目会话的系统提示；每次任务前自动重读，保存后下一条消息即生效。</source>
        <translation>One AGENTS.md per project, injected into the system prompt of its sessions. Re-read before every task, so a save takes effect on the next message.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ProjectRulesPage.qml" line="264"/>
        <source>尚未导入任何项目。先在侧栏导入项目文件夹，即可为它配置规则。</source>
        <translation>No projects imported yet. Import a project folder from the sidebar to give it rules.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ProjectRulesPage.qml" line="286"/>
        <source>AGENTS.md — %1</source>
        <translation>AGENTS.md — %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ProjectRulesPage.qml" line="74"/>
        <source>生效中</source>
        <translation>Active</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ProjectRulesPage.qml" line="75"/>
        <source>超过注入上限，截断注入</source>
        <translation>Over the injection cap, injected truncated</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ProjectRulesPage.qml" line="76"/>
        <source>模板未编辑，未注入</source>
        <translation>Template untouched, not injected</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ProjectRulesPage.qml" line="77"/>
        <source>规则为空，未注入</source>
        <translation>Rules empty, not injected</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ProjectRulesPage.qml" line="78"/>
        <source>未创建</source>
        <translation>Not created</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ProjectRulesPage.qml" line="296"/>
        <source>%1 / %2 字节</source>
        <translation>%1 / %2 bytes</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ProjectRulesPage.qml" line="346"/>
        <source>在系统编辑器中打开</source>
        <translation>Open in system editor</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ProjectRulesPage.qml" line="441"/>
        <source>AGENTS.md 尚未创建，输入内容保存后将自动创建并注入会话</source>
        <translation>AGENTS.md does not exist yet. Type something and it will be created and injected.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ProjectRulesPage.qml" line="442"/>
        <source>写下希望模型始终遵守的项目约定（Markdown）</source>
        <translation>Write the conventions the model should always follow (Markdown)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ProjectRulesPage.qml" line="444"/>
        <source>项目规则内容</source>
        <translation>Project rules content</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ProjectRulesPage.qml" line="465"/>
        <source>规则计入上下文预算，注入上限 %1 KB，超出部分自动截断；未编辑的模板不会被注入。</source>
        <translation>Rules count toward the context budget; injection caps at %1 KB and truncates beyond it. An untouched template is never injected.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ProjectRulesPage.qml" line="490"/>
        <source>从上方选择一个项目，查看并编辑它的 AGENTS.md</source>
        <translation>Select a project above to view and edit its AGENTS.md</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ProjectRulesPage.qml" line="378"/>
        <source>AGENTS.md 已被外部编辑器修改，重新加载将丢弃本地未保存的修改。</source>
        <translation>AGENTS.md was changed by an external editor. Reloading discards your unsaved edits.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ProjectRulesPage.qml" line="386"/>
        <source>重新加载</source>
        <translation>Reload</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ProjectRulesPage.qml" line="403"/>
        <source>保留我的修改</source>
        <translation>Keep my edits</translation>
    </message>
</context>
<context>
    <name>PromptAssetsPage</name>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="23"/>
        <source>提示词与指令页</source>
        <translation>Prompts &amp; commands page</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="45"/>
        <source>预设包已导入，立即生效（无需重启）。</source>
        <translation>Preset pack imported. Changes take effect immediately (no restart needed).</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="46"/>
        <source>导入失败。</source>
        <translation>Import failed.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="79"/>
        <source>提示词与指令</source>
        <translation>Prompts &amp; commands</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="80"/>
        <source>系统提示预设、斜杠指令与会话模板。改动即时保存。</source>
        <translation>System prompt presets, slash commands and session templates. Changes save instantly.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="94"/>
        <source>导出预设包…</source>
        <translation>Export preset pack…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="99"/>
        <source>导入预设包…</source>
        <translation>Import preset pack…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="145"/>
        <source>系统提示预设</source>
        <translation>System prompt presets</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="151"/>
        <source>新建预设</source>
        <translation>New preset</translation>
    </message>
    <message>
        <source>整段角色设定（如「严谨的技术评审」）。通过顶栏的预设按钮套用到当前会话；覆盖全局系统提示词，删除预设不影响已套用的会话。</source>
        <translation type="vanished">A full persona (e.g. &quot;rigorous code reviewer&quot;). Apply it to the current session from the top-bar button; it overrides the global system prompt, and deleting a preset never affects sessions already applied.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="159"/>
        <source>整段角色设定（如「严谨的技术评审」）。在输入框下方的预设选择器里套用到当前会话；覆盖全局系统提示词，删除预设不影响已套用的会话。</source>
        <translation>The full role definition (e.g. &quot;rigorous technical reviewer&quot;). Apply it to the current session from the preset selector below the input; it overrides the global system prompt, and deleting a preset does not affect sessions already using it.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="195"/>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="345"/>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="528"/>
        <source>编辑</source>
        <translation>Edit</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="199"/>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="349"/>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="532"/>
        <source>删除</source>
        <translation>Delete</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="220"/>
        <source>还没有预设。点「新建预设」写下第一段角色设定。</source>
        <translation>No presets yet. Click &quot;New preset&quot; to write your first persona.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="234"/>
        <source>预设名称（如：技术评审）</source>
        <translation>Preset name (e.g. Code reviewer)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="245"/>
        <source>完整的系统提示词内容…</source>
        <translation>Full system prompt content…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="257"/>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="429"/>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="626"/>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="830"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="261"/>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="433"/>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="630"/>
        <source>创建</source>
        <translation>Create</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="261"/>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="433"/>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="630"/>
        <source>保存</source>
        <translation>Save</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="289"/>
        <source>斜杠指令</source>
        <translation>Slash commands</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="295"/>
        <source>新建指令</source>
        <translation>New command</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="303"/>
        <source>输入框以「/指令」开头发送即展开为模板。模板中的 {text} 会被替换为指令后输入的内容；没有占位符时内容附在模板之后。</source>
        <translation>A message starting with /command is expanded from the template before sending. {text} in the template receives whatever you typed after the command; without a placeholder the text is appended.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="369"/>
        <source>还没有指令。例：/review + 模板「请审查以下代码，指出问题：{text}」。</source>
        <translation>No commands yet. Example: /review with template &quot;Review this code and point out issues: {text}&quot;.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="381"/>
        <source>/</source>
        <translation>/</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="386"/>
        <source>指令名（不含斜杠，如 review）</source>
        <translation>Command word (no slash, e.g. review)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="400"/>
        <source>说明（可选，显示在指令列表）</source>
        <translation>Description (optional, shown in the list)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="417"/>
        <source>展开模板…含 {text} 占位符</source>
        <translation>Expansion template… may contain {text}</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="462"/>
        <source>会话模板</source>
        <translation>Session templates</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="468"/>
        <source>新建模板</source>
        <translation>New template</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="476"/>
        <source>三个入口都能用：侧栏「新对话」右缘的箭头、会话标签栏「+」的右键/长按、以及空会话页的「从模板开始」。一键按预置的模型配置、权限模式与首条消息开新会话；首条消息进入输入框待确认，不会自动发送。</source>
        <translation>Three entry points: the arrow on the right edge of &quot;New chat&quot; in the sidebar, right-click/long-press on the tab bar &quot;+&quot;, and &quot;Start from a template&quot; on the empty chat page. One click starts a session with the preset model profile, permission mode and first message; the first message lands in the composer for review and is never sent automatically.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="488"/>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="599"/>
        <source>权限不变</source>
        <translation>Keep permission</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="489"/>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="600"/>
        <source>只读计划</source>
        <translation>Plan only</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="490"/>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="601"/>
        <source>变更确认</source>
        <translation>Confirm changes</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="491"/>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="602"/>
        <source>自动编辑</source>
        <translation>Auto edit</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="492"/>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="603"/>
        <source>完全访问</source>
        <translation>Full access</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="495"/>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="500"/>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="590"/>
        <source>配置不变</source>
        <translation>Keep model profile</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="560"/>
        <source>还没有模板。例：「代码审查」= 只读计划 + 首条消息「请审查这个项目的近期改动」。</source>
        <translation>No templates yet. Example: &quot;Code review&quot; = plan only + first message &quot;Review this project&apos;s recent changes&quot;.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="573"/>
        <source>模板名称（如：代码审查）</source>
        <translation>Template name (e.g. Code review)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="614"/>
        <source>首条消息（可选，进入输入框待确认）</source>
        <translation>First message (optional, placed in the composer for confirmation)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="688"/>
        <source>导出预设包</source>
        <translation>Export preset pack</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="691"/>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="707"/>
        <source>预设包 (*.json)</source>
        <translation>Preset pack (*.json)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="696"/>
        <source>导出失败。</source>
        <translation>Export failed.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="697"/>
        <source>预设包已导出（%1 条预设、%2 条斜杠指令）。不含任何密钥。</source>
        <translation>Preset pack exported (%1 presets, %2 slash commands). No credentials included.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="705"/>
        <source>选择预设包</source>
        <translation>Select preset pack</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="715"/>
        <source>无法预览此预设包。</source>
        <translation>Could not preview this preset pack.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="743"/>
        <source>导入预设包</source>
        <translation>Import preset pack</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="750"/>
        <source>预设 %1 条 · 斜杠指令 %2 条 · 禁用工具 %3 个 · 默认权限模式 %4</source>
        <translation>%1 presets · %2 slash commands · %3 disabled tools · default permission mode %4</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="756"/>
        <source>不修改</source>
        <translation>Leave unchanged</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="764"/>
        <source>与现有条目重名 %1 条：</source>
        <translation>%1 entries share a name with existing items:</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="779"/>
        <source>预设：%1</source>
        <translation>Preset: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="780"/>
        <source>指令：/%1</source>
        <translation>Command: /%1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="789"/>
        <source>…等 %1 条</source>
        <translation>…and %1 more</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="798"/>
        <source>同名条目处理</source>
        <translation>Handle items with the same name</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="809"/>
        <source>全部重命名（保留两边）</source>
        <translation>Rename all (keep both)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="810"/>
        <source>覆盖本机同名条目</source>
        <translation>Overwrite local items with the same name</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="811"/>
        <source>跳过本机已有的</source>
        <translation>Skip items already on this machine</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/PromptAssetsPage.qml" line="839"/>
        <source>导入</source>
        <translation>Import</translation>
    </message>
</context>
<context>
    <name>PromptPresetSelector</name>
    <message>
        <location filename="../src/ui/components/PromptPresetSelector.qml" line="49"/>
        <source>提示词·默认</source>
        <translation>Prompt · default</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PromptPresetSelector.qml" line="47"/>
        <source>预设已删除 · %1</source>
        <translation>Preset deleted · %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PromptPresetSelector.qml" line="58"/>
        <source>提示词预设</source>
        <translation>Prompt preset</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PromptPresetSelector.qml" line="60"/>
        <source>当前会话套用预设「%1」，覆盖设置里的全局系统提示词</source>
        <translation>This session applies preset &quot;%1&quot;, overriding the global system prompt from settings</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PromptPresetSelector.qml" line="61"/>
        <source>当前会话未套用预设，使用设置里的全局系统提示词</source>
        <translation>No preset applied to this session; using the global system prompt from settings</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PromptPresetSelector.qml" line="204"/>
        <source>提示词预设 · 当前会话</source>
        <translation>Prompt presets · this session</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PromptPresetSelector.qml" line="211"/>
        <source>已套用</source>
        <translation>Applied</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PromptPresetSelector.qml" line="211"/>
        <source>跟随全局</source>
        <translation>Follows global</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PromptPresetSelector.qml" line="258"/>
        <location filename="../src/ui/components/PromptPresetSelector.qml" line="291"/>
        <source>无预设（使用全局系统提示词）</source>
        <translation>No preset (use the global system prompt)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PromptPresetSelector.qml" line="268"/>
        <source>设置页已配置自定义全局提示词</source>
        <translation>A custom global prompt is configured in settings</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PromptPresetSelector.qml" line="269"/>
        <source>设置页未配置，使用内置提示词</source>
        <translation>Nothing configured in settings; the built-in prompt is used</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PromptPresetSelector.qml" line="406"/>
        <source>还没有预设。用下面的「管理预设…」写下第一段角色设定。</source>
        <translation>No presets yet. Use &quot;Manage presets…&quot; below to write your first persona.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/PromptPresetSelector.qml" line="455"/>
        <location filename="../src/ui/components/PromptPresetSelector.qml" line="475"/>
        <source>管理预设…</source>
        <translation>Manage presets…</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../src/main.cpp" line="990"/>
        <source>显示/隐藏主窗</source>
        <translation>Show / hide main window</translation>
    </message>
    <message>
        <location filename="../src/main.cpp" line="991"/>
        <source>新建会话</source>
        <translation>New session</translation>
    </message>
    <message>
        <source>模型配置</source>
        <translation type="vanished">Model profiles</translation>
    </message>
    <message>
        <source>（暂无配置）</source>
        <translation type="vanished">(No profiles yet)</translation>
    </message>
    <message>
        <location filename="../src/main.cpp" line="993"/>
        <source>退出</source>
        <translation>Quit</translation>
    </message>
    <message>
        <location filename="../src/main.cpp" line="1054"/>
        <source>任务完成</source>
        <translation>Task finished</translation>
    </message>
    <message>
        <location filename="../src/main.cpp" line="1054"/>
        <source>任务结束</source>
        <translation>Task ended</translation>
    </message>
    <message>
        <location filename="../src/main.cpp" line="1055"/>
        <source>会话已回复</source>
        <translation>The session has replied</translation>
    </message>
    <message>
        <location filename="../src/main.cpp" line="1070"/>
        <source>今日 Token</source>
        <translation>Today&apos;s tokens</translation>
    </message>
    <message>
        <location filename="../src/main.cpp" line="1072"/>
        <source>今日费用</source>
        <translation>Today&apos;s cost</translation>
    </message>
    <message>
        <location filename="../src/main.cpp" line="1074"/>
        <source>本月 Token</source>
        <translation>This month&apos;s tokens</translation>
    </message>
    <message>
        <location filename="../src/main.cpp" line="1076"/>
        <source>本月费用</source>
        <translation>This month&apos;s cost</translation>
    </message>
    <message>
        <location filename="../src/main.cpp" line="1080"/>
        <source>用量接近配额上限</source>
        <translation>Usage nearing quota</translation>
    </message>
    <message>
        <location filename="../src/main.cpp" line="1081"/>
        <source>%1 已用 %2 / %3</source>
        <translation>%1: %2 of %3 used</translation>
    </message>
    <message>
        <location filename="../src/main.cpp" line="1143"/>
        <source>自动备份失败</source>
        <translation>Automatic backup failed</translation>
    </message>
    <message>
        <location filename="../src/main.cpp" line="1144"/>
        <source>无法写入数据库快照目录：%1</source>
        <translation>Could not write the database snapshot to: %1</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="94"/>
        <source>PDF 已加密或需要密码</source>
        <translation>PDF is encrypted or password-protected</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="97"/>
        <source>PDF 文件已损坏或格式无效</source>
        <translation>PDF file is corrupted or invalid</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="100"/>
        <source>PDF 没有文本层（可能是扫描件）</source>
        <translation>PDF has no text layer (possibly a scanned copy)</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="103"/>
        <source>PDF 读取失败</source>
        <translation>Failed to read the PDF</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="114"/>
        <source>无法打开文件</source>
        <translation>Cannot open the file</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="123"/>
        <source>内容为空或无可用分段</source>
        <translation>Content is empty or has no usable segments</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="130"/>
        <source>相同内容的文档已存在</source>
        <translation>A document with identical content already exists</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="143"/>
        <source>嵌入服务不可用，已降级为本地伪嵌入（可稍后重新嵌入）</source>
        <translation>Embedding service unavailable; degraded to local pseudo-embedding (re-embed later)</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="149"/>
        <source>文档过大，超过单文档分段上限</source>
        <translation>Document too large for the per-document segment cap</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="152"/>
        <source>超出知识库总段数预算</source>
        <translation>Exceeds the knowledge-base chunk budget</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/AgentViewModel.cpp" line="155"/>
        <source>导入失败</source>
        <translation>Import failed</translation>
    </message>
</context>
<context>
    <name>ReferenceChipRow</name>
    <message>
        <location filename="../src/ui/components/ReferenceChipRow.qml" line="25"/>
        <source>未绑定项目，无法解析引用</source>
        <translation>No project is bound, so references cannot be resolved</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ReferenceChipRow.qml" line="26"/>
        <source>项目内未找到该文件</source>
        <translation>File not found in the project</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ReferenceChipRow.qml" line="27"/>
        <source>敏感文件不可引用</source>
        <translation>Sensitive files cannot be referenced</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ReferenceChipRow.qml" line="28"/>
        <source>路径不在项目目录内</source>
        <translation>Path is outside the project directory</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ReferenceChipRow.qml" line="30"/>
        <source>无法引用</source>
        <translation>Cannot reference this file</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ReferenceChipRow.qml" line="52"/>
        <source>引用</source>
        <translation>References</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ReferenceChipRow.qml" line="107"/>
        <source>%1（%2）</source>
        <translation>%1 (%2)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ReferenceChipRow.qml" line="109"/>
        <source>该文件将随消息发送：%1</source>
        <translation>This file will be sent with the message: %1</translation>
    </message>
</context>
<context>
    <name>ReviewDialog</name>
    <message>
        <location filename="../src/ui/components/ReviewDialog.qml" line="87"/>
        <location filename="../src/ui/components/ReviewDialog.qml" line="106"/>
        <source>文件改动审阅</source>
        <translation>Review file changes</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ReviewDialog.qml" line="88"/>
        <source>按目录列出本次会话改动的文件，并显示所选文件的内容差异</source>
        <translation>Lists the files changed in this session by folder, and shows the content diff of the selected file.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ReviewDialog.qml" line="117"/>
        <source>%1 个文件</source>
        <translation>%1 files</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ReviewDialog.qml" line="150"/>
        <source>全部确认</source>
        <translation>Accept All</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ReviewDialog.qml" line="155"/>
        <source>确认本会话全部待确认的文件改动</source>
        <translation>Accept every pending file change in this session</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ReviewDialog.qml" line="165"/>
        <source>全部撤销</source>
        <translation>Revert All</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ReviewDialog.qml" line="171"/>
        <source>撤销本会话全部待确认的文件改动</source>
        <translation>Revert every pending file change in this session</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ReviewDialog.qml" line="188"/>
        <source>折叠全部目录</source>
        <translation>Collapse all folders</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ReviewDialog.qml" line="199"/>
        <source>展开全部目录</source>
        <translation>Expand all folders</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ReviewDialog.qml" line="208"/>
        <source>关闭审阅对话框</source>
        <translation>Close the review dialog</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ReviewDialog.qml" line="259"/>
        <source>本次会话暂无可审阅的文件改动。</source>
        <translation>No file changes to review in this session.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ReviewDialog.qml" line="260"/>
        <source>智能体写入或编辑文件后，这里会按目录列出改动；
选中文件即可在右侧查看内容差异。</source>
        <translation>After the agent writes or edits files they are listed here by folder;
select a file to view its content diff on the right.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ReviewDialog.qml" line="275"/>
        <source>关闭</source>
        <translation>Close</translation>
    </message>
</context>
<context>
    <name>SecurityPage</name>
    <message>
        <location filename="../src/ui/pages/SecurityPage.qml" line="13"/>
        <source>安全设置页</source>
        <translation>Security Settings</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SecurityPage.qml" line="15"/>
        <source>安全</source>
        <translation>Security</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SecurityPage.qml" line="16"/>
        <source>本页修改实时保存。判定顺序：系统目录封锁 → 黑名单 → 允许层（用户目录 ∪ 当前项目）。</source>
        <translation>Changes on this page are saved instantly. Evaluation order: system directories → deny list → allow layers (user directories ∪ active project).</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SecurityPage.qml" line="20"/>
        <source>文件读取沙箱</source>
        <translation>File Read Sandbox</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SecurityPage.qml" line="24"/>
        <location filename="../src/ui/pages/SecurityPage.qml" line="30"/>
        <source>允许读取整个磁盘</source>
        <translation>Allow reading the entire disk</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SecurityPage.qml" line="25"/>
        <source>关闭时读取仅限文档/下载/桌面/应用目录与当前项目；开启后除系统目录与黑名单外全部可读，模型读到的内容可能发送给云端模型。</source>
        <translation>When off, reads are limited to Documents/Downloads/Desktop/AppData and the active project; when on, everything except system directories and the deny list is readable, and anything the model reads may be sent to the cloud model.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SecurityPage.qml" line="37"/>
        <location filename="../src/ui/pages/SecurityPage.qml" line="43"/>
        <source>允许写入当前项目目录</source>
        <translation>Allow writing into the current project folder</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SecurityPage.qml" line="38"/>
        <source>默认项目目录只读，写入仅限用户目录；开启后模型可以修改当前项目中的文件（写入类工具仍受会话权限模式约束，黑名单始终优先）。</source>
        <translation>Project folders are read-only by default and writes are limited to your user folders. When enabled, the model may modify files in the current project (write tools are still bound by the session permission mode, and the deny list always wins).</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SecurityPage.qml" line="50"/>
        <source>黑名单（始终拒绝，优先于一切允许规则）</source>
        <translation>Deny list (always denied, overrides every allow rule)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SecurityPage.qml" line="58"/>
        <source>内置默认拦截：~/.ssh、~/.gnupg、~/.aws 及 *.pem / *.key / *.ppk / *.kdbx。以下为追加项，支持目录（或目录名，命中任意层级）与通配模式如 *.env。</source>
        <translation>Built-in defaults: ~/.ssh, ~/.gnupg, ~/.aws and *.pem / *.key / *.ppk / *.kdbx. Entries below are user additions: a directory (or a directory name matching at any depth) or a glob such as *.env.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SecurityPage.qml" line="148"/>
        <source>移除黑名单条目 %1</source>
        <translation>Remove deny-list entry %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SecurityPage.qml" line="158"/>
        <source>暂无追加项</source>
        <translation>No user entries</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SecurityPage.qml" line="169"/>
        <source>目录路径、目录名或通配模式（如 D:secrets、.git、*.env）</source>
        <translation>Directory path, directory name, or glob (e.g. D:secrets, .git, *.env)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SecurityPage.qml" line="183"/>
        <source>新增黑名单条目</source>
        <translation>New deny-list entry</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SecurityPage.qml" line="189"/>
        <source>添加</source>
        <translation>Add</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SecurityPage.qml" line="215"/>
        <source>隐私门</source>
        <translation>Privacy Gates</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SecurityPage.qml" line="219"/>
        <location filename="../src/ui/pages/SecurityPage.qml" line="225"/>
        <source>仅本地模式</source>
        <translation>Local-only Mode</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SecurityPage.qml" line="220"/>
        <source>使用云端模型时拦截知识库搜索、联网搜索与全部 MCP 工具（工具页会显示拦截徽标）；本地模型不受影响。</source>
        <translation>With cloud models this blocks knowledge search, web search and all MCP tools (a badge shows on the Tools page); local models are unaffected.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SecurityPage.qml" line="231"/>
        <location filename="../src/ui/pages/SecurityPage.qml" line="237"/>
        <source>允许 http_fetch 访问本机与内网地址</source>
        <translation>Allow http_fetch to reach localhost and intranet</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SecurityPage.qml" line="232"/>
        <source>关闭时 http_fetch 拒绝本机/内网目标（防 SSRF）；仅在确有访问内网服务的需求时开启。</source>
        <translation>When off, http_fetch rejects local/intranet targets (SSRF guard); enable only if intranet access is truly needed.</translation>
    </message>
</context>
<context>
    <name>SessionTemplateMenu</name>
    <message>
        <location filename="../src/ui/components/SessionTemplateMenu.qml" line="150"/>
        <source>会话模板</source>
        <translation>Session templates</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SessionTemplateMenu.qml" line="157"/>
        <source>共 %1 个</source>
        <translation>%1 total</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SessionTemplateMenu.qml" line="92"/>
        <source>配置已删除</source>
        <translation>Profile deleted</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SessionTemplateMenu.qml" line="72"/>
        <location filename="../src/ui/components/SessionTemplateMenu.qml" line="82"/>
        <source>权限不变</source>
        <translation>Keep permission</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SessionTemplateMenu.qml" line="73"/>
        <source>只读计划</source>
        <translation>Plan only</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SessionTemplateMenu.qml" line="74"/>
        <source>变更确认</source>
        <translation>Confirm Changes</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SessionTemplateMenu.qml" line="75"/>
        <source>自动编辑</source>
        <translation>Auto Edit</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SessionTemplateMenu.qml" line="76"/>
        <source>完全访问</source>
        <translation>Full Access</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SessionTemplateMenu.qml" line="86"/>
        <location filename="../src/ui/components/SessionTemplateMenu.qml" line="90"/>
        <source>配置不变</source>
        <translation>Keep model profile</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SessionTemplateMenu.qml" line="195"/>
        <location filename="../src/ui/components/SessionTemplateMenu.qml" line="215"/>
        <source>空白新对话</source>
        <translation>Blank new chat</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SessionTemplateMenu.qml" line="366"/>
        <location filename="../src/ui/components/SessionTemplateMenu.qml" line="386"/>
        <source>管理模板…</source>
        <translation>Manage templates…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SessionTemplateMenu.qml" line="317"/>
        <source>还没有模板。在「设置 → 提示词与指令」里新建，例如「代码审查」＝只读计划 + 首条消息。</source>
        <translation>No templates yet. Create one under Settings → Prompts &amp; commands, e.g. &quot;Code review&quot; = plan-only + a first message.</translation>
    </message>
    <message>
        <source>从会话模板开始</source>
        <translation type="vanished">Start from a session template</translation>
    </message>
    <message>
        <source>按预置的模型配置、权限模式与首条消息新建会话</source>
        <translation type="vanished">Start a session with a preset model profile, permission mode and first message</translation>
    </message>
    <message>
        <source>从模板开始</source>
        <translation type="vanished">Start from a template</translation>
    </message>
    <message>
        <source>点击新建空白对话；右键或长按从会话模板开始</source>
        <translation type="vanished">Click for a blank new chat; right-click or long-press to start from a template</translation>
    </message>
    <message>
        <source>三个入口都能用：侧栏「新对话」右缘的箭头、会话标签栏「+」的右键/长按、以及空会话页的「从模板开始」。一键按预置的模型配置、权限模式与首条消息开新会话；首条消息进入输入框待确认，不会自动发送。</source>
        <translation type="vanished">Three entry points: the arrow on the right edge of &quot;New chat&quot; in the sidebar, right-click/long-press on the tab bar &quot;+&quot;, and &quot;Start from a template&quot; on the empty chat page. One click starts a session with the preset model profile, permission mode and first message; the first message lands in the composer for review and is never sent automatically.</translation>
    </message>
</context>
<context>
    <name>SettingRow</name>
    <message>
        <location filename="../src/ui/components/SettingRow.qml" line="99"/>
        <source>恢复默认值</source>
        <translation>Restore default</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingRow.qml" line="103"/>
        <source>将「%1」恢复为默认值</source>
        <translation>Restore “%1” to its default value</translation>
    </message>
</context>
<context>
    <name>SettingsBackupPage</name>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="11"/>
        <source>设置与备份页</source>
        <translation>Settings and backup page</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="80"/>
        <source>设置与备份</source>
        <translation>Settings and Backup</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="81"/>
        <source>将应用设置保存为 JSON 文件，或从备份文件恢复。已知密钥字段和 SQLite 数据（如聊天记录与长期记忆）不包含在内；自由文本及 MCP 命令参数可能含有敏感内容。</source>
        <translation>Save app settings to a JSON file or restore them from a backup. Known secret fields and SQLite data (such as chat history and long-term memory) are excluded; free-form text and MCP command arguments may contain sensitive information.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="86"/>
        <source>自动备份</source>
        <translation>Automatic backup</translation>
    </message>
    <message>
        <source>应用启动时自动生成一份数据库快照；只保留最近指定份数，更早的自动删除。</source>
        <translation type="vanished">A database snapshot is created at startup; only the most recent N copies are kept and older ones are deleted automatically.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="91"/>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="97"/>
        <source>启动时自动备份</source>
        <translation>Back up on startup</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="92"/>
        <source>关闭后仅在手动点击「立即备份」时生成快照。</source>
        <translation>When off, snapshots are created only when you click “Back up now”.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="109"/>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="118"/>
        <source>备份保留数量</source>
        <translation>Backups to keep</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="110"/>
        <source>轮转时只保留最新的指定份数，范围 1–20。</source>
        <translation>Rotation keeps only the most recent N backups; range 1–20.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="163"/>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="345"/>
        <source>导出设置</source>
        <translation>Export Settings</translation>
    </message>
    <message>
        <source>选择保存位置，生成可用于迁移设置的 JSON 备份。请妥善保管备份文件。</source>
        <translation type="vanished">Choose where to save a JSON backup for migrating your settings. Keep the backup file safe.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="187"/>
        <source>导出 JSON…</source>
        <translation>Export JSON…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="195"/>
        <source>从备份恢复</source>
        <translation>Restore from Backup</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="196"/>
        <source>先预览将恢复的设置，再确认导入；高风险权限及开机自启需在恢复后重新确认。建议恢复后重启应用。</source>
        <translation>Preview the settings to be restored before confirming the import. Review high-risk permissions and launch at startup again afterward. Restarting the app is recommended.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="199"/>
        <source>选择 JSON 备份…</source>
        <translation>Select JSON Backup…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="208"/>
        <source>数据库备份</source>
        <translation>Database backup</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="209"/>
        <source>聊天记录、思考轨迹与知识库保存在 SQLite 数据库中，设置 JSON 备份不包含它们。启动时会自动备份并保留最近 %1 份；此处可随时手动备份，或预览任一备份文件的内容概况。</source>
        <translation>Chats, thinking traces and the knowledge base live in the SQLite database and are not covered by the settings JSON backup. A backup is taken at startup and the latest %1 are kept; back up manually here at any time, or preview the contents of any backup file.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="214"/>
        <source>立即备份</source>
        <translation>Back up now</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="220"/>
        <source>数据库已备份到 %1。</source>
        <translation>Database backed up to %1.</translation>
    </message>
    <message>
        <source>预览备份文件…</source>
        <translation type="vanished">Preview a backup file…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="348"/>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="470"/>
        <source>JSON 文件 (*.json)</source>
        <translation>JSON files (*.json)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="353"/>
        <source>导出失败。</source>
        <translation>Export failed.</translation>
    </message>
    <message>
        <source>设置已导出。已知密钥字段已过滤；请检查自由文本和 MCP 命令参数。</source>
        <translation type="vanished">Settings exported. Known secret fields have been filtered out; review free-form text and MCP command arguments.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="468"/>
        <source>选择设置备份</source>
        <translation>Select Settings Backup</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="479"/>
        <source>无法预览此备份文件。</source>
        <translation>Unable to preview this backup file.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="491"/>
        <source>选择数据库备份</source>
        <translation>Choose database backup</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="493"/>
        <source>SQLite 数据库 (*.db)</source>
        <translation>SQLite database (*.db)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="52"/>
        <source>无法读取该备份文件：%1</source>
        <translation>Cannot read this backup file: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="58"/>
        <source>会话 %1 个，消息 %2 条，知识库文档 %3 份，最新消息时间 %4%5</source>
        <translation>%1 sessions, %2 messages, %3 knowledge documents; newest message at %4%5</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="64"/>
        <source>（无消息）</source>
        <translation>(no messages)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="67"/>
        <source>；警告：完整性检查未通过，可能已损坏</source>
        <translation>; warning: integrity check failed, the file may be corrupted</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="566"/>
        <source>恢复数据库</source>
        <translation>Restore database</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="573"/>
        <source>将把当前数据库替换为该备份：会话 %1 个、消息 %2 条。恢复会关闭全部标签并停止在跑任务，期间界面会短暂无响应。当前未备份的新数据将丢失（启动自动备份仍可用）。</source>
        <translation>This replaces the current database with the backup: %1 sessions, %2 messages. Restoring closes all tabs and stops running tasks; the UI is briefly unresponsive. New data not backed up yet is lost (startup automatic backups remain available).</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="313"/>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="590"/>
        <source>恢复</source>
        <translation>Restore</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="33"/>
        <source>全部设置</source>
        <translation>All settings</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="37"/>
        <source>、</source>
        <translation>, </translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="87"/>
        <source>应用启动时可生成一份数据库快照，也可按固定间隔定时备份；只保留最近指定份数，更早的自动删除。</source>
        <translation>Create a database snapshot at startup, or back up on a fixed schedule. Only the most recent snapshots are kept; older ones are deleted automatically.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="133"/>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="155"/>
        <source>定时备份间隔</source>
        <translation>Scheduled backup interval</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="134"/>
        <source>应用运行期间按此间隔生成快照；关闭后只保留启动备份与手动备份。</source>
        <translation>Create a snapshot at this interval while the app is running; when off, only startup and manual backups are made.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="140"/>
        <source>关闭</source>
        <translation>Off</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="140"/>
        <source>每 1 小时</source>
        <translation>Every hour</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="140"/>
        <source>每 6 小时</source>
        <translation>Every 6 hours</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="141"/>
        <source>每 1 天</source>
        <translation>Every day</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="141"/>
        <source>每 7 天</source>
        <translation>Every 7 days</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="164"/>
        <source>选择保存位置，生成可用于迁移设置的 JSON 备份。可按分类只导出需要的部分；请妥善保管备份文件。</source>
        <translation>Choose a location to create a JSON backup for moving settings. You can export only the categories you need; keep the backup file safe.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="173"/>
        <source>导出范围：%1</source>
        <translation>Export scope: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="182"/>
        <source>选择范围…</source>
        <translation>Choose scope…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="226"/>
        <source>打开备份目录</source>
        <translation>Open backup folder</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="230"/>
        <source>从其他位置恢复…</source>
        <translation>Restore from another location…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="235"/>
        <source>刷新</source>
        <translation>Refresh</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="249"/>
        <source>备份目录里还没有快照。开启「启动时自动备份」后，应用每次启动都会生成一份。</source>
        <translation>No snapshots in the backup folder yet. With “Back up on startup” enabled, one is created on every launch.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="278"/>
        <source>自动</source>
        <translation>Auto</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="278"/>
        <source>手动</source>
        <translation>Manual</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="309"/>
        <source>预览</source>
        <translation>Preview</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="319"/>
        <source>删除此备份</source>
        <translation>Delete this backup</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="321"/>
        <source>删除备份 %1</source>
        <translation>Delete backup %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="354"/>
        <source>设置已导出（%1）。已知密钥字段已过滤；请检查自由文本和 MCP 命令参数。</source>
        <translation>Settings exported (%1). Known key fields were filtered out; review free-text and MCP command arguments.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="393"/>
        <source>选择导出范围</source>
        <translation>Choose export scope</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="401"/>
        <source>只导出选中的分类；一个都不选等于导出全部设置。模型配置与 MCP 服务器分别归入「模型配置」「MCP 服务器」。</source>
        <translation>Only the selected categories are exported; selecting none exports all settings. Model profiles and MCP servers belong to “Model configuration” and “MCP servers” respectively.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="450"/>
        <source>确定</source>
        <translation>OK</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="512"/>
        <source>删除这份备份？</source>
        <translation>Delete this backup?</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="520"/>
        <source>备份文件将从备份目录中永久删除，无法撤销。</source>
        <translation>The backup file will be permanently deleted from the backup folder. This cannot be undone.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="535"/>
        <source>删除</source>
        <translation>Delete</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="543"/>
        <source>备份已删除。</source>
        <translation>Backup deleted.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="599"/>
        <source>数据库已恢复，知识库正在重新装载。</source>
        <translation>Database restored; the knowledge base is reloading.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="621"/>
        <source>确认恢复设置？</source>
        <translation>Restore settings?</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="638"/>
        <source>即将恢复的分类：%1</source>
        <translation>Categories to restore: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="640"/>
        <source>无</source>
        <translation>None</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="653"/>
        <source>将被改变的设置（%1 项）：</source>
        <translation>Settings that will change (%1):</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="655"/>
        <source>备份中的设置与当前值完全一致，恢复不会改变任何值。</source>
        <translation>The backup matches the current values exactly; restoring will not change anything.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="727"/>
        <source>另有 %1 项变更未列出。</source>
        <translation>%1 more changes are not listed.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="737"/>
        <source>备份不含以下密钥：%1</source>
        <translation>Secrets not included in the backup: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="747"/>
        <source>备份提示：%1</source>
        <translation>Backup notes: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="756"/>
        <source>匹配相同端点的本机密钥会保留；新端点需要重新配置密钥。SQLite 数据不会恢复。恢复会覆盖相应设置，MCP 服务立即禁用；请重启应用并重新确认高风险权限和开机自启。</source>
        <translation>Local secrets for matching endpoints are retained; configure secrets again for new endpoints. SQLite data is not restored. Settings are overwritten and MCP servers are disabled immediately. Restart the app and review high-risk permissions and launch at startup.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="446"/>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="531"/>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="586"/>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="771"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="775"/>
        <source>确认恢复</source>
        <translation>Confirm Restore</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="786"/>
        <source>恢复失败。</source>
        <translation>Restore failed.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="788"/>
        <source>设置已恢复，请重启应用后检查高风险权限和开机自启。</source>
        <translation>Settings restored. Restart the app, then check high-risk permissions and launch at startup.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/SettingsBackupPage.qml" line="789"/>
        <source>设置已恢复，建议重启应用并重新确认高风险权限和开机自启。</source>
        <translation>Settings restored. Restarting the app and reviewing high-risk permissions and launch at startup are recommended.</translation>
    </message>
</context>
<context>
    <name>SettingsCenterDialog</name>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="127"/>
        <source>已保存</source>
        <translation>Saved</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="142"/>
        <source>已撤销对「%1」的更改</source>
        <translation>Reverted the change to &quot;%1&quot;</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="145"/>
        <source>无法撤销上次更改</source>
        <translation>Could not undo the last change</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="161"/>
        <source>已恢复「%1」的默认值</source>
        <translation>Restored defaults for “%1”</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="243"/>
        <source>已恢复全部设置（密钥与代理凭据保持不变）</source>
        <translation>All settings restored (API keys and proxy credentials are unchanged)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="437"/>
        <source>设置中心</source>
        <translation>Settings center</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="438"/>
        <source>配置模型、界面、工具、MCP 服务器等应用选项</source>
        <translation>Configure model, interface, tools, MCP servers and other application options</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="456"/>
        <source>设置</source>
        <translation>Settings</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="468"/>
        <source>搜索设置…</source>
        <translation>Search settings…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="483"/>
        <source>搜索设置</source>
        <translation>Search settings</translation>
    </message>
    <message>
        <source>输入关键词筛选设置；Ctrl+F 可随时聚焦搜索框</source>
        <translation type="vanished">Type keywords to filter settings; press Ctrl+F to focus the search box</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="510"/>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="756"/>
        <source>清除搜索</source>
        <translation>Clear search</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="570"/>
        <source>找到 %1 个设置</source>
        <translation>Found %1 settings</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="579"/>
        <source>↑↓ 选择 · Enter 跳转 · Esc 清除</source>
        <translation>↑↓ to select · Enter to open · Esc to clear</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="702"/>
        <source>跳转</source>
        <translation>Go to</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="702"/>
        <source>Enter 跳转</source>
        <translation>Enter to open</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="738"/>
        <source>没有匹配「%1」的设置</source>
        <translation>No settings match &quot;%1&quot;</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="996"/>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="1303"/>
        <source>恢复全部设置</source>
        <translation>Restore all settings</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="998"/>
        <source>将所有分类的设置恢复为默认值</source>
        <translation>Restore every category to its defaults</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="1049"/>
        <source>撤销</source>
        <translation>Undo</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="1050"/>
        <source>撤销刚才对「%1」的更改</source>
        <translation>Undo the change just made to &quot;%1&quot;</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="1231"/>
        <source>恢复默认值</source>
        <translation>Restore defaults</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="1257"/>
        <source>将所有设置恢复为默认值？</source>
        <translation>Restore all settings to their defaults?</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="1270"/>
        <source>模型、界面、智能体、网络、安全、工具、记忆、知识库、用量、日志等分类的设置都会立即写入默认值。该操作不可撤销。</source>
        <translation>Settings in Model, Interface, Agent, Network, Security, Tools, Memory, Knowledge, Usage and Logs are written back to their defaults immediately. This cannot be undone.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="1281"/>
        <source>已保存的模型、搜索与向量 API 密钥，以及代理用户名与密码不会被清除；清除密钥只能在各页面的字段旁操作。</source>
        <translation>Saved model, search and embedding API keys, plus the proxy username and password, are not cleared. Keys can only be removed from the field on their page.</translation>
    </message>
    <message>
        <source>恢复默认</source>
        <translation type="vanished">Restore Defaults</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="1058"/>
        <source>关闭</source>
        <translation>Close</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="747"/>
        <source>换个关键词，或清除搜索返回分类页。</source>
        <translation>Try another keyword, or clear the search to return to the categories.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="484"/>
        <source>输入关键词筛选设置</source>
        <translation>Type a keyword to filter settings</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="983"/>
        <source>恢复本页默认值</source>
        <translation>Restore page defaults</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="987"/>
        <source>将「%1」的设置恢复为默认值</source>
        <translation>Restore the settings of “%1” to defaults</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="1170"/>
        <source>将「%1」恢复为默认值？</source>
        <translation>Restore “%1” to defaults?</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="1184"/>
        <source>本页设置会立即写入默认值，操作不可撤销。</source>
        <translation>This page&apos;s settings will be reset to defaults immediately; this cannot be undone.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="1195"/>
        <source>已保存的模型、搜索与向量 API 密钥不会被清除；如需删除密钥，请使用字段旁的「清除密钥」。</source>
        <translation>Saved model, search, and embedding API keys are not cleared; to delete a key, use &quot;Clear key&quot; next to the field.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="1209"/>
        <source>注意：代理用户名与密码会一并恢复为默认（清空）。</source>
        <translation>Note: the proxy username and password will also be reset to default (cleared).</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="1227"/>
        <location filename="../src/ui/components/SettingsCenterDialog.qml" line="1299"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>保存</source>
        <translation type="vanished">Save</translation>
    </message>
    <message>
        <source>保存失败，请检查输入</source>
        <translation type="vanished">Save failed, please check your input</translation>
    </message>
    <message>
        <source>此页修改即时保存</source>
        <translation type="vanished">Changes on this page are saved instantly</translation>
    </message>
    <message>
        <source>有未保存的修改，确认放弃？</source>
        <translation type="vanished">You have unsaved changes. Discard them?</translation>
    </message>
    <message>
        <source>继续编辑</source>
        <translation type="vanished">Keep Editing</translation>
    </message>
    <message>
        <source>放弃</source>
        <translation type="vanished">Discard</translation>
    </message>
    <message>
        <source>确认将当前分类的配置项恢复为默认值？</source>
        <translation type="vanished">Restore the settings in this category to their defaults?</translation>
    </message>
</context>
<context>
    <name>SettingsField</name>
    <message>
        <location filename="../src/ui/components/SettingsField.qml" line="187"/>
        <source>隐藏</source>
        <translation>Hide</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsField.qml" line="187"/>
        <source>显示</source>
        <translation>Show</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsField.qml" line="193"/>
        <source>显示或隐藏密钥</source>
        <translation>Show or hide the key</translation>
    </message>
</context>
<context>
    <name>SettingsSearch</name>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="218"/>
        <source>项目规则</source>
        <translation>Project rules</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="42"/>
        <source>配置来源</source>
        <translation>Profile Source</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="45"/>
        <source>服务连接</source>
        <translation>Connection</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="48"/>
        <source>模型</source>
        <translation>Models</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="51"/>
        <source>生成参数</source>
        <translation>Generation</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="54"/>
        <source>思考与上下文</source>
        <translation>Reasoning and Context</translation>
    </message>
    <message>
        <source>模型行为</source>
        <translation type="vanished">Model Behavior</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="75"/>
        <source>自定义系统提示词</source>
        <translation>Custom System Prompt</translation>
    </message>
    <message>
        <source>日志</source>
        <translation type="vanished">Logging</translation>
    </message>
    <message>
        <source>网络代理</source>
        <translation type="vanished">HTTP Proxy</translation>
    </message>
    <message>
        <source>任务预算</source>
        <translation type="vanished">Task budget</translation>
    </message>
    <message>
        <source>知识库嵌入</source>
        <translation type="vanished">Knowledge Embedding</translation>
    </message>
    <message>
        <source>文件读取沙箱</source>
        <translation type="vanished">File Read Sandbox</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="147"/>
        <source>允许写入当前项目目录</source>
        <translation>Allow writing into the current project folder</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="150"/>
        <source>黑名单</source>
        <translation>Blacklist</translation>
    </message>
    <message>
        <source>隐私门</source>
        <translation type="vanished">Privacy Gates</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="116"/>
        <source>MCP 服务器</source>
        <translation>MCP Servers</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="94"/>
        <source>工具管理</source>
        <translation>Tool management</translation>
    </message>
    <message>
        <source>联网搜索</source>
        <translation type="vanished">Web Search</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="110"/>
        <source>默认权限模式</source>
        <translation>Default Permission Mode</translation>
    </message>
    <message>
        <source>长期记忆</source>
        <translation type="vanished">Long-term Memory</translation>
    </message>
    <message>
        <source>数据保留</source>
        <translation type="vanished">Data Retention</translation>
    </message>
    <message>
        <source>配额设置</source>
        <translation type="vanished">Quota Settings</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="244"/>
        <source>用量统计</source>
        <translation>Usage Statistics</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="249"/>
        <source>活动记录</source>
        <translation>Activity Log</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="254"/>
        <source>日志级别</source>
        <translation>Log Level</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="257"/>
        <source>日志目录</source>
        <translation>Log directory</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="260"/>
        <source>单个日志文件上限</source>
        <translation>Max log file size</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="263"/>
        <source>保留日志文件数</source>
        <translation>Number of log files to keep</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="268"/>
        <source>设置导出与导入</source>
        <translation>Settings Export and Import</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="271"/>
        <source>数据库备份</source>
        <translation>Database backup</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="198"/>
        <source>系统提示预设</source>
        <translation>System prompt presets</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="37"/>
        <source>导航分组折叠状态</source>
        <translation>Navigation group collapse state</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="57"/>
        <source>模型配置数据</source>
        <translation>Model profile data</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="60"/>
        <source>当前模型配置</source>
        <translation>Active model profile</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="63"/>
        <source>上下文预算</source>
        <translation>Context Budget</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="68"/>
        <source>单次任务步数预算</source>
        <translation>Step budget per task</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="72"/>
        <source>单次任务超时（分钟）</source>
        <translation>Task timeout (minutes)</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="80"/>
        <source>子代理开关</source>
        <translation>Sub-agent toggle</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="83"/>
        <source>子代理并行上限</source>
        <translation>Sub-agent parallelism cap</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="86"/>
        <source>子代理步数预算</source>
        <translation>Sub-agent step budget</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="89"/>
        <source>子代理超时（秒）</source>
        <translation>Sub-agent timeout (seconds)</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="97"/>
        <source>联网搜索服务</source>
        <translation>Web search provider</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="101"/>
        <source>联网搜索 API Key</source>
        <translation>Web search API key</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="104"/>
        <source>智谱搜索密钥</source>
        <translation>Zhipu search API key</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="107"/>
        <source>DeepSeek 搜索密钥</source>
        <translation>DeepSeek search API key</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="121"/>
        <source>代理模式</source>
        <translation>Proxy mode</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="124"/>
        <source>代理服务器地址</source>
        <translation>Proxy server address</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="127"/>
        <source>代理端口</source>
        <translation>Proxy port</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="130"/>
        <source>代理用户名</source>
        <translation>Proxy username</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="133"/>
        <source>代理密码</source>
        <translation>Proxy password</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="136"/>
        <source>代理认证数据</source>
        <translation>Proxy credentials</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="139"/>
        <source>测试代理</source>
        <translation>Test proxy</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="144"/>
        <source>允许读取整个磁盘</source>
        <translation>Allow reading the entire disk</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="153"/>
        <source>仅本地模式</source>
        <translation>Local-only mode</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="156"/>
        <source>允许 http_fetch 访问本机与内网地址</source>
        <translation>Allow http_fetch to access localhost and intranet addresses</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="161"/>
        <source>启用长期记忆</source>
        <translation>Enable long-term memory</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="164"/>
        <source>审计日志保留天数</source>
        <translation>Audit log retention (days)</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="167"/>
        <source>Token 用量保留天数</source>
        <translation>Token usage retention (days)</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="170"/>
        <source>长期记忆列表</source>
        <translation>Long-term memory list</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="173"/>
        <source>启动时自动备份</source>
        <translation>Back up on startup</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="176"/>
        <source>备份保留数量</source>
        <translation>Backup retention count</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="179"/>
        <source>定时备份间隔</source>
        <translation>Scheduled backup interval</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="184"/>
        <source>嵌入服务提供方</source>
        <translation>Embedding provider</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="187"/>
        <source>嵌入模型</source>
        <translation>Embedding Model</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="190"/>
        <source>嵌入服务地址</source>
        <translation>Embedding Endpoint</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="193"/>
        <source>向量模型 API Key</source>
        <translation>Embedding API key</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="201"/>
        <source>斜杠指令</source>
        <translation>Slash commands</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="204"/>
        <source>会话模板</source>
        <translation>Session templates</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="207"/>
        <source>提示词预设数据</source>
        <translation>Prompt preset data</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="210"/>
        <source>斜杠指令数据</source>
        <translation>Slash command data</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="213"/>
        <source>会话模板数据</source>
        <translation>Session template data</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="221"/>
        <source>当前项目</source>
        <translation>Current project</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="226"/>
        <source>每日 Token 上限</source>
        <translation>Daily Token Limit</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="229"/>
        <source>每月 Token 上限</source>
        <translation>Monthly Token Limit</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="232"/>
        <source>每日费用上限</source>
        <translation>Daily cost limit</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="235"/>
        <source>每月费用上限</source>
        <translation>Monthly cost limit</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="238"/>
        <source>超限拒绝请求</source>
        <translation>Reject at limit</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="241"/>
        <source>用量告警阈值</source>
        <translation>Usage alert threshold</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="276"/>
        <source>侧边栏宽度</source>
        <translation>Sidebar width</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="279"/>
        <source>面板宽度</source>
        <translation>Panel width</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="282"/>
        <source>面板可见</source>
        <translation>Panel visible</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="285"/>
        <source>侧边栏展开</source>
        <translation>Sidebar expanded</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="288"/>
        <source>显示过程</source>
        <translation>Show process</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="291"/>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="293"/>
        <source>窗口位置</source>
        <translation>Window position</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="295"/>
        <source>窗口宽度</source>
        <translation>Window width</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="297"/>
        <source>窗口高度</source>
        <translation>Window height</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="299"/>
        <source>窗口最大化</source>
        <translation>Window maximized</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="302"/>
        <source>设置版本</source>
        <translation>Settings version</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsCatalog.cpp" line="305"/>
        <source>旧版深色主题</source>
        <translation>Legacy dark theme</translation>
    </message>
</context>
<context>
    <name>SettingsSection</name>
    <message>
        <location filename="../src/ui/components/SettingsSection.qml" line="47"/>
        <source>已折叠，按空格展开</source>
        <translation>Collapsed, press Space to expand</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SettingsSection.qml" line="47"/>
        <source>已展开，按空格折叠</source>
        <translation>Expanded, press Space to collapse</translation>
    </message>
</context>
<context>
    <name>SettingsValue</name>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="103"/>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="106"/>
        <source>跟随系统</source>
        <translation>Follow system</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="104"/>
        <source>深色</source>
        <translation>Dark</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="105"/>
        <source>浅色</source>
        <translation>Light</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="107"/>
        <source>手动</source>
        <translation>Manual</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="108"/>
        <source>关闭</source>
        <translation>Off</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="109"/>
        <source>调试</source>
        <translation>Debug</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="110"/>
        <source>信息</source>
        <translation>Info</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="111"/>
        <source>警告</source>
        <translation>Warning</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="112"/>
        <source>严重</source>
        <translation>Critical</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="114"/>
        <source>每次都确认</source>
        <translation>Always confirm</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="116"/>
        <source>自动编辑</source>
        <translation>Auto edit</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="118"/>
        <source>完全访问</source>
        <translation>Full access</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="120"/>
        <source>仅计划</source>
        <translation>Plan only</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="121"/>
        <source>本地 Ollama</source>
        <translation>Local Ollama</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="122"/>
        <source>OpenAI 兼容</source>
        <translation>OpenAI compatible</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="209"/>
        <source>已配置</source>
        <translation>Configured</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="210"/>
        <source>未配置</source>
        <translation>Not configured</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="219"/>
        <source>全部默认</source>
        <translation>All defaults</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="220"/>
        <source>%1 项自定义</source>
        <translation>%1 customized</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="228"/>
        <source>开</source>
        <translation>On</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="229"/>
        <source>关</source>
        <translation>Off</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="233"/>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="250"/>
        <source>%1 项</source>
        <translation>%1 items</translation>
    </message>
    <message>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="240"/>
        <location filename="../src/viewmodels/SettingsValueFormat.cpp" line="254"/>
        <source>（空）</source>
        <translation>(empty)</translation>
    </message>
</context>
<context>
    <name>ShortcutsDialog</name>
    <message>
        <location filename="../src/ui/components/ShortcutsDialog.qml" line="32"/>
        <source>快捷键速查对话框</source>
        <translation>Keyboard shortcut cheat sheet</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ShortcutsDialog.qml" line="33"/>
        <source>查看全部快捷键</source>
        <translation>View all keyboard shortcuts</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ShortcutsDialog.qml" line="39"/>
        <source>快捷键</source>
        <translation>Shortcut</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ShortcutsDialog.qml" line="49"/>
        <source>全部快捷键一览</source>
        <translation>All keyboard shortcuts at a glance</translation>
    </message>
    <message>
        <source>打开设置中心</source>
        <translation type="vanished">Open settings</translation>
    </message>
    <message>
        <source>设置中心内搜索（设置打开时）</source>
        <translation type="vanished">Search inside Settings (while open)</translation>
    </message>
    <message>
        <source>打开审计日志</source>
        <translation type="vanished">Open audit log</translation>
    </message>
    <message>
        <source>显示/隐藏上下文面板</source>
        <translation type="vanished">Show / hide context panel</translation>
    </message>
    <message>
        <source>打开文件改动审阅</source>
        <translation type="vanished">Open the file change review</translation>
    </message>
    <message>
        <source>打开任务清单</source>
        <translation type="vanished">Open the task list</translation>
    </message>
    <message>
        <source>显示/隐藏侧边栏</source>
        <translation type="vanished">Show / hide sidebar</translation>
    </message>
    <message>
        <source>切换深色/浅色主题</source>
        <translation type="vanished">Toggle dark / light theme</translation>
    </message>
    <message>
        <source>打开本速查表</source>
        <translation type="vanished">Open this cheat sheet</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ShortcutsDialog.qml" line="22"/>
        <source>发送消息</source>
        <translation>Send message</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ShortcutsDialog.qml" line="23"/>
        <source>发送消息（输入法组合键安全）</source>
        <translation>Send message (IME-safe)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ShortcutsDialog.qml" line="24"/>
        <source>输入换行</source>
        <translation>Insert newline</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ShortcutsDialog.qml" line="25"/>
        <source>回溯/前进输入历史</source>
        <translation>Go back / forward in input history</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ShortcutsDialog.qml" line="26"/>
        <source>粘贴图片附件</source>
        <translation>Paste image attachment</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ShortcutsDialog.qml" line="72"/>
        <source>全局快捷键</source>
        <translation>Global shortcuts</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ShortcutsDialog.qml" line="93"/>
        <source>输入框内固定按键</source>
        <translation>Fixed keys in the input box</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ShortcutsDialog.qml" line="122"/>
        <source>关闭</source>
        <translation>Close</translation>
    </message>
</context>
<context>
    <name>ShortcutsPage</name>
    <message>
        <location filename="../src/ui/pages/ShortcutsPage.qml" line="15"/>
        <source>快捷键设置页</source>
        <translation>Shortcuts settings page</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ShortcutsPage.qml" line="17"/>
        <source>快捷键</source>
        <translation>Shortcuts</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ShortcutsPage.qml" line="18"/>
        <source>点击「录制」后按下新的组合键。全局快捷键必须包含 Ctrl、Alt 或 Meta，或使用 F1–F12 功能键；Esc 取消录制。</source>
        <translation>Click Record, then press the new key combination. Global shortcuts need Ctrl, Alt, or Meta, or an F1–F12 function key; Esc cancels recording.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ShortcutsPage.qml" line="53"/>
        <source>快捷键已更新</source>
        <translation>Shortcut updated</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ShortcutsPage.qml" line="56"/>
        <source>快捷键未能更新</source>
        <translation>Could not update the shortcut</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ShortcutsPage.qml" line="75"/>
        <source>全局快捷键</source>
        <translation>Global shortcuts</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ShortcutsPage.qml" line="81"/>
        <source>自定义快捷键</source>
        <translation>Custom shortcuts</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ShortcutsPage.qml" line="82"/>
        <source>修改立即保存；恢复默认只影响快捷键，不动其他设置。</source>
        <translation>Changes save immediately; restoring defaults only affects shortcuts.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ShortcutsPage.qml" line="89"/>
        <source>%1 项已自定义</source>
        <translation>%1 customized</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ShortcutsPage.qml" line="90"/>
        <source>全部为默认</source>
        <translation>All defaults</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ShortcutsPage.qml" line="128"/>
        <source>按下组合键…</source>
        <translation>Press keys…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ShortcutsPage.qml" line="139"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ShortcutsPage.qml" line="140"/>
        <source>录制</source>
        <translation>Record</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ShortcutsPage.qml" line="153"/>
        <source>恢复</source>
        <translation>Restore</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ShortcutsPage.qml" line="160"/>
        <source>已恢复「%1」的默认快捷键</source>
        <translation>Restored the default shortcut for &quot;%1&quot;</translation>
    </message>
</context>
<context>
    <name>Sidebar</name>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="1391"/>
        <source>更多操作</source>
        <translation>More actions</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="1392"/>
        <source>归档、导出或结束这条对话</source>
        <translation>Archive, export or end this conversation</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="974"/>
        <location filename="../src/ui/components/Sidebar.qml" line="1531"/>
        <location filename="../src/ui/components/Sidebar.qml" line="1552"/>
        <source>结束对话</source>
        <translation>End conversation</translation>
    </message>
    <message>
        <source>关闭这条对话的视图，历史仍保留</source>
        <translation type="vanished">Close this conversation&apos;s view; history is kept</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="912"/>
        <location filename="../src/ui/components/Sidebar.qml" line="1133"/>
        <location filename="../src/ui/components/Sidebar.qml" line="1676"/>
        <source>新会话</source>
        <translation>New Session</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="778"/>
        <source>搜索会话历史…</source>
        <translation>Search session history…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="787"/>
        <source>搜索会话历史</source>
        <translation>Search session history</translation>
    </message>
    <message>
        <source>搜索结果</source>
        <translation type="vanished">Search Results</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="738"/>
        <source>暂无历史会话</source>
        <translation>No sessions yet</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="553"/>
        <location filename="../src/ui/components/Sidebar.qml" line="1280"/>
        <source>确认?</source>
        <translation>Confirm?</translation>
    </message>
    <message>
        <source>导入项目…</source>
        <translation type="vanished">Import Project…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="834"/>
        <source>选择项目文件夹</source>
        <translation>Choose Project Folder</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="332"/>
        <source>导入项目</source>
        <translation>Import Project</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="195"/>
        <location filename="../src/ui/components/Sidebar.qml" line="263"/>
        <source>新对话</source>
        <translation>New Chat</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="231"/>
        <source>从会话模板开始</source>
        <translation>Start from a session template</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="232"/>
        <source>按预置的模型配置、权限模式与首条消息新建会话</source>
        <translation>Start a session with a preset model profile, permission mode and first message</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="300"/>
        <source>PROJECTS</source>
        <translation>PROJECTS</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="496"/>
        <source>在此项目下新建会话</source>
        <translation>New session in this project</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="526"/>
        <source>重命名项目</source>
        <translation>Rename Project</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="604"/>
        <source>暂无会话</source>
        <translation>No sessions yet</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="618"/>
        <source>暂无项目。点上方 + 选择文件夹导入</source>
        <translation>No projects yet. Click + above to import a folder</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="685"/>
        <source>对话</source>
        <translation>Chats</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="716"/>
        <source>新建本地对话</source>
        <translation>New Local Chat</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="720"/>
        <source>本地对话</source>
        <translation>Local Chats</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="800"/>
        <source>包含已归档</source>
        <translation>Include archived</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="946"/>
        <location filename="../src/ui/components/Sidebar.qml" line="1210"/>
        <source>取消置顶</source>
        <translation>Unpin</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="946"/>
        <location filename="../src/ui/components/Sidebar.qml" line="1210"/>
        <source>置顶会话</source>
        <translation>Pin conversation</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="950"/>
        <location filename="../src/ui/components/Sidebar.qml" line="1440"/>
        <location filename="../src/ui/components/Sidebar.qml" line="1461"/>
        <source>恢复会话</source>
        <translation>Restore conversation</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="950"/>
        <location filename="../src/ui/components/Sidebar.qml" line="1440"/>
        <location filename="../src/ui/components/Sidebar.qml" line="1461"/>
        <source>归档会话</source>
        <translation>Archive conversation</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="957"/>
        <location filename="../src/ui/components/Sidebar.qml" line="1484"/>
        <location filename="../src/ui/components/Sidebar.qml" line="1504"/>
        <source>导出会话</source>
        <translation>Export conversation</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="964"/>
        <location filename="../src/ui/components/Sidebar.qml" line="1250"/>
        <source>重命名会话</source>
        <translation>Rename conversation</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="979"/>
        <location filename="../src/ui/components/Sidebar.qml" line="1314"/>
        <source>删除会话</source>
        <translation>Delete conversation</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="1315"/>
        <source>需要点击两次确认</source>
        <translation>Click again to confirm</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="1697"/>
        <source>会话 %1，%2 条命中</source>
        <translation>Conversation %1, %2 matches</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="1758"/>
        <source>%1 条命中</source>
        <translation>%1 matches</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="1871"/>
        <source>知识库（正在处理）</source>
        <translation>Knowledge base (busy)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="1872"/>
        <source>知识库</source>
        <translation>Knowledge Base</translation>
    </message>
    <message>
        <source>会话操作</source>
        <translation type="vanished">Conversation actions</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="1571"/>
        <source>已置顶 · </source>
        <translation>Pinned · </translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="1572"/>
        <location filename="../src/ui/components/Sidebar.qml" line="1681"/>
        <source>已归档 · </source>
        <translation>Archived · </translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="1622"/>
        <location filename="../src/ui/components/Sidebar.qml" line="1627"/>
        <source>无匹配结果</source>
        <translation>No matches</translation>
    </message>
    <message>
        <source>用户</source>
        <translation type="vanished">User</translation>
    </message>
    <message>
        <source>助手</source>
        <translation type="vanished">Assistant</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="1831"/>
        <source>日志</source>
        <translation>Logs</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="1839"/>
        <source>审计日志</source>
        <translation>Audit Log</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="1847"/>
        <source>设置 (Ctrl+,)</source>
        <translation>Settings (Ctrl+,)</translation>
    </message>
    <message>
        <source>深色模式</source>
        <translation type="vanished">Dark mode</translation>
    </message>
    <message>
        <source>当前为深色主题，按回车切换到浅色</source>
        <translation type="vanished">Dark theme is active; press Return to switch to light</translation>
    </message>
    <message>
        <source>当前为浅色主题，按回车切换到深色</source>
        <translation type="vanished">Light theme is active; press Return to switch to dark</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="1886"/>
        <source>Markdown (*.md)</source>
        <translation>Markdown (*.md)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/Sidebar.qml" line="1886"/>
        <source>JSON (*.json)</source>
        <translation>JSON (*.json)</translation>
    </message>
    <message>
        <source>空白新对话</source>
        <translation type="vanished">Blank new chat</translation>
    </message>
    <message>
        <source>还没有模板</source>
        <translation type="vanished">No templates yet</translation>
    </message>
</context>
<context>
    <name>SizePresetRow</name>
    <message>
        <source>自动</source>
        <translation type="vanished">Auto</translation>
    </message>
</context>
<context>
    <name>SplitBar</name>
    <message>
        <location filename="../src/ui/components/SplitBar.qml" line="108"/>
        <source>拖动调节高度 · 双击复位</source>
        <translation>Drag to resize · Double-click to reset</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SplitBar.qml" line="109"/>
        <source>拖动调节宽度 · 双击复位</source>
        <translation>Drag to resize · Double-click to reset</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SplitBar.qml" line="112"/>
        <source>面板分隔条</source>
        <translation>Panel separator bar</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SplitBar.qml" line="112"/>
        <source>详情分隔条</source>
        <translation>Detail divider</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SplitBar.qml" line="114"/>
        <source>拖动调节详情区域高度，双击复位</source>
        <translation>Drag to adjust the detail area height, double-click to reset</translation>
    </message>
    <message>
        <location filename="../src/ui/components/SplitBar.qml" line="115"/>
        <source>拖动调节面板宽度，双击复位</source>
        <translation>Drag to adjust panel width, double-click to reset</translation>
    </message>
</context>
<context>
    <name>SqliteMemory</name>
    <message>
        <location filename="../src/core/memory/SqliteMemory.cpp" line="100"/>
        <source>刚刚</source>
        <translation>Just now</translation>
    </message>
    <message>
        <location filename="../src/core/memory/SqliteMemory.cpp" line="101"/>
        <source>%1 分钟前</source>
        <translation>%1 minutes ago</translation>
    </message>
    <message>
        <location filename="../src/core/memory/SqliteMemory.cpp" line="102"/>
        <source>%1 小时前</source>
        <translation>%1 hours ago</translation>
    </message>
    <message>
        <location filename="../src/core/memory/SqliteMemory.cpp" line="103"/>
        <source>昨天</source>
        <translation>Yesterday</translation>
    </message>
    <message>
        <location filename="../src/core/memory/SqliteMemory.cpp" line="1443"/>
        <source>消息库超过 %1 条：少于 3 个字符的关键词只能全表扫描，请输入至少 3 个字符以使用全文索引。</source>
        <translation>The message store exceeds %1 rows: keywords shorter than 3 characters fall back to a full table scan. Enter at least 3 characters to use the full-text index.</translation>
    </message>
</context>
<context>
    <name>ThinkingEffortSelector</name>
    <message>
        <location filename="../src/ui/components/ThinkingEffortSelector.qml" line="23"/>
        <location filename="../src/ui/components/ThinkingEffortSelector.qml" line="40"/>
        <source>思考·默认</source>
        <translation>Thinking - Default</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ThinkingEffortSelector.qml" line="23"/>
        <source>不发送思考参数，由模型自己决定</source>
        <translation>Send no thinking parameter; the model decides</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ThinkingEffortSelector.qml" line="24"/>
        <source>思考·关闭</source>
        <translation>Thinking - Off</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ThinkingEffortSelector.qml" line="24"/>
        <source>明确要求不要思考，响应最快、最省 token</source>
        <translation>Explicitly request no thinking; fastest and cheapest responses</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ThinkingEffortSelector.qml" line="25"/>
        <source>思考·低</source>
        <translation>Thinking - Low</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ThinkingEffortSelector.qml" line="25"/>
        <source>少量推理预算，适合简单问答</source>
        <translation>A small reasoning budget; good for simple questions</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ThinkingEffortSelector.qml" line="26"/>
        <source>思考·中</source>
        <translation>Thinking - Medium</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ThinkingEffortSelector.qml" line="26"/>
        <source>均衡；工具型任务推荐</source>
        <translation>Balanced; recommended for tool-driven tasks</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ThinkingEffortSelector.qml" line="27"/>
        <source>思考·高</source>
        <translation>Thinking - High</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ThinkingEffortSelector.qml" line="27"/>
        <source>最大推理投入，慢且贵，适合难题</source>
        <translation>Maximum reasoning effort; slow and costly, for hard problems</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ThinkingEffortSelector.qml" line="50"/>
        <source>思考强度</source>
        <translation>Thinking effort</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ThinkingEffortSelector.qml" line="99"/>
        <source>思考·未配置模型</source>
        <translation>Effort · no model set</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ThinkingEffortSelector.qml" line="155"/>
        <source>思考强度 · 当前会话</source>
        <translation>Reasoning effort · This conversation</translation>
    </message>
    <message>
        <source>思考强度 · 保存到当前模型配置</source>
        <translation type="vanished">Thinking effort - saved to the current model profile</translation>
    </message>
</context>
<context>
    <name>TodoDialog</name>
    <message>
        <location filename="../src/ui/components/TodoDialog.qml" line="120"/>
        <source>任务清单对话框</source>
        <translation>Task list dialog</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoDialog.qml" line="121"/>
        <source>查看当前会话的任务清单与完成进度</source>
        <translation>Review the task list and the completion progress of the current session</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoDialog.qml" line="147"/>
        <source>任务清单 · %1/%2 已完成</source>
        <translation>Tasks · %1/%2 completed</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoDialog.qml" line="149"/>
        <source>任务清单</source>
        <translation>Tasks</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoDialog.qml" line="162"/>
        <location filename="../src/ui/components/TodoDialog.qml" line="266"/>
        <source>关闭任务清单</source>
        <translation>Close the task list</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoDialog.qml" line="196"/>
        <source>当前会话还没有任务清单。</source>
        <translation>This session has no task list yet.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoDialog.qml" line="207"/>
        <source>代理调用 todo_write 记录任务后，任务会出现在这里。</source>
        <translation>Tasks show up here once the agent records them with todo_write.</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoDialog.qml" line="264"/>
        <source>关闭</source>
        <translation>Close</translation>
    </message>
</context>
<context>
    <name>TodoListPanel</name>
    <message>
        <location filename="../src/ui/components/TodoListPanel.qml" line="65"/>
        <source>进行中 %1</source>
        <translation>In progress %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoListPanel.qml" line="67"/>
        <source>待处理 %1</source>
        <translation>Pending %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoListPanel.qml" line="125"/>
        <source>已完成</source>
        <translation>Completed</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoListPanel.qml" line="126"/>
        <source>进行中</source>
        <translation>In progress</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoListPanel.qml" line="127"/>
        <source>待处理</source>
        <translation>Pending</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoListPanel.qml" line="131"/>
        <source>高优先级</source>
        <translation>High priority</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoListPanel.qml" line="132"/>
        <source>中优先级</source>
        <translation>Medium priority</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoListPanel.qml" line="133"/>
        <source>低优先级</source>
        <translation>Low priority</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoListPanel.qml" line="197"/>
        <source>任务清单</source>
        <translation>Tasks</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoListPanel.qml" line="210"/>
        <source>%1/%2 完成</source>
        <translation>%1/%2 done</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoListPanel.qml" line="228"/>
        <source>任务清单，%1/%2 完成</source>
        <translation>Tasks, %1/%2 done</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoListPanel.qml" line="230"/>
        <source>已折叠</source>
        <translation>Collapsed</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoListPanel.qml" line="230"/>
        <source>已展开</source>
        <translation>Expanded</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoListPanel.qml" line="235"/>
        <source>展开任务清单</source>
        <translation>Expand the task list</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoListPanel.qml" line="235"/>
        <source>折叠任务清单</source>
        <translation>Collapse the task list</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoListPanel.qml" line="253"/>
        <source>任务进度：%1/%2 已完成</source>
        <translation>Task progress: %1/%2 completed</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TodoListPanel.qml" line="284"/>
        <source>全部完成</source>
        <translation>All done</translation>
    </message>
</context>
<context>
    <name>ToolConfirmDialog</name>
    <message>
        <location filename="../src/ui/components/ToolConfirmDialog.qml" line="80"/>
        <source>工具执行确认对话框</source>
        <translation>Tool execution confirmation dialog</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ToolConfirmDialog.qml" line="81"/>
        <source>AI 请求执行高危操作 %1，请选择允许或拒绝</source>
        <translation>AI requests to execute high-risk operation %1, please choose allow or deny</translation>
    </message>
    <message>
        <source>AI 请求执行高危操作</source>
        <translation type="vanished">The AI requests to run a high-risk operation</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ToolConfirmDialog.qml" line="100"/>
        <source>确认工具操作</source>
        <translation>Confirm tool action</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ToolConfirmDialog.qml" line="112"/>
        <source>来自会话：%1 · 待处理 %2 项</source>
        <translation>Conversation: %1 · %2 pending</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ToolConfirmDialog.qml" line="125"/>
        <source>工具名称:</source>
        <translation>Tool Name:</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ToolConfirmDialog.qml" line="144"/>
        <source>调用参数:</source>
        <translation>Arguments:</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ToolConfirmDialog.qml" line="144"/>
        <source>变更预览:</source>
        <translation>Change preview:</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ToolConfirmDialog.qml" line="169"/>
        <source>拒绝</source>
        <translation>Deny</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ToolConfirmDialog.qml" line="175"/>
        <source>允许执行</source>
        <translation>Allow Execution</translation>
    </message>
</context>
<context>
    <name>ToolRow</name>
    <message>
        <location filename="../src/ui/components/ToolRow.qml" line="45"/>
        <source>内置</source>
        <translation>Built-in</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ToolRow.qml" line="51"/>
        <source>自动</source>
        <translation>Auto</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ToolRow.qml" line="52"/>
        <source>需确认</source>
        <translation>Requires confirmation</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ToolRow.qml" line="53"/>
        <source>危险</source>
        <translation>Dangerous</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ToolRow.qml" line="64"/>
        <source>仅本地模式拦截</source>
        <translation>Blocked by Local-only Mode</translation>
    </message>
    <message>
        <location filename="../src/ui/components/ToolRow.qml" line="85"/>
        <source>启用工具 %1</source>
        <translation>Enable tool %1</translation>
    </message>
</context>
<context>
    <name>ToolsMemoryPage</name>
    <message>
        <source>工具与记忆设置页</source>
        <translation type="vanished">Tools &amp; Memory settings page</translation>
    </message>
    <message>
        <source>配置联网搜索与长期记忆</source>
        <translation type="vanished">Configure web search and long-term memory</translation>
    </message>
    <message>
        <source>工具与记忆</source>
        <translation type="vanished">Tools &amp; Memory</translation>
    </message>
    <message>
        <source>本页修改实时保存，不受底部「保存 / 取消」影响</source>
        <translation type="vanished">Changes on this page save instantly and are not affected by the &quot;Save / Cancel&quot; buttons below</translation>
    </message>
    <message>
        <source>长期记忆</source>
        <translation type="vanished">Long-term Memory</translation>
    </message>
    <message>
        <source>开启后，每次任务结束会生成会话摘要并在新会话中注入相关历史，帮助模型记住此前聊过的内容。</source>
        <translation type="vanished">When enabled, a session summary is generated after each task and relevant history is injected into new sessions, helping the model remember what was discussed before.</translation>
    </message>
    <message>
        <source>启用长期记忆</source>
        <translation type="vanished">Enable long-term memory</translation>
    </message>
    <message>
        <source>联网搜索</source>
        <translation type="vanished">Web Search</translation>
    </message>
    <message>
        <source>启用后模型可调用 web_search 获取实时信息。搜索查询会发送至所配置的搜索服务商，请勿在查询中包含敏感信息。</source>
        <translation type="vanished">When enabled, the model can call web_search to fetch real-time information. Search queries are sent to the configured search provider; do not include sensitive information in your queries.</translation>
    </message>
    <message>
        <source>搜索服务</source>
        <translation type="vanished">Search provider</translation>
    </message>
    <message>
        <source>搜索 API Key</source>
        <translation type="vanished">Search API key</translation>
    </message>
    <message>
        <source>已配置（输入新值可覆盖，留空不修改）</source>
        <translation type="vanished">Configured (enter a new value to overwrite, leave blank to keep)</translation>
    </message>
    <message>
        <source>未配置</source>
        <translation type="vanished">Not configured</translation>
    </message>
    <message>
        <source>清除密钥</source>
        <translation type="vanished">Clear key</translation>
    </message>
    <message>
        <source>清除搜索 API Key</source>
        <translation type="vanished">Clear search API key</translation>
    </message>
</context>
<context>
    <name>ToolsPage</name>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="10"/>
        <source>工具管理页</source>
        <translation>Tool management page</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="12"/>
        <source>工具管理</source>
        <translation>Tool management</translation>
    </message>
    <message>
        <source>控制哪些工具可被 AI 调用。禁用的工具不会暴露给模型。</source>
        <translation type="vanished">Control which tools can be called by AI. Disabled tools are not exposed to the model.</translation>
    </message>
    <message>
        <source>内置</source>
        <translation type="vanished">Built-in</translation>
    </message>
    <message>
        <source>自动</source>
        <translation type="vanished">Auto</translation>
    </message>
    <message>
        <source>需确认</source>
        <translation type="vanished">Requires confirmation</translation>
    </message>
    <message>
        <source>危险</source>
        <translation type="vanished">Dangerous</translation>
    </message>
    <message>
        <source>仅本地模式拦截</source>
        <translation type="vanished">Blocked by Local-only Mode</translation>
    </message>
    <message>
        <source>启用工具 %1</source>
        <translation type="vanished">Enable tool %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="142"/>
        <source>联网搜索</source>
        <translation>Web Search</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="143"/>
        <source>启用后模型可调用 web_search 获取实时信息。搜索查询会发送至所配置的搜索服务商，请勿在查询中包含敏感信息。</source>
        <translation>When enabled, the model can call web_search to fetch real-time information. Search queries are sent to the configured search provider; do not include sensitive information in your queries.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="147"/>
        <location filename="../src/ui/pages/ToolsPage.qml" line="166"/>
        <source>搜索服务</source>
        <translation>Search provider</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="182"/>
        <source>搜索 API Key</source>
        <translation>Search API key</translation>
    </message>
    <message>
        <source>已配置（输入新值可覆盖，留空不修改）</source>
        <translation type="vanished">Configured (enter a new value to overwrite, leave blank to keep)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="190"/>
        <source>未配置</source>
        <translation>Not configured</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="199"/>
        <source>清除密钥</source>
        <translation>Clear key</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="202"/>
        <source>清除搜索 API Key</source>
        <translation>Clear search API key</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="214"/>
        <source>测试连接</source>
        <translation>Test connection</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="221"/>
        <source>测试搜索服务连接</source>
        <translation>Test search service connection</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="45"/>
        <source>DuckDuckGo（免密钥）</source>
        <translation>DuckDuckGo (no API key)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="13"/>
        <source>控制哪些工具可被 AI 调用。禁用的工具不会暴露给模型；内置工具默认折叠，MCP 工具单独成组。</source>
        <translation>Control which tools the AI can call. Disabled tools are hidden from the model; built-in tools are collapsed by default, and MCP tools are grouped separately.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="46"/>
        <source>智谱 Web Search</source>
        <translation>Zhipu Web Search</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="47"/>
        <source>DeepSeek Search</source>
        <translation>DeepSeek Search</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="55"/>
        <source>搜索工具名称或说明…</source>
        <translation>Search tool names or descriptions…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="62"/>
        <source>搜索工具</source>
        <translation>Search tools</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="81"/>
        <source>内置工具</source>
        <translation>Built-in tools</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="82"/>
        <source>随应用提供的工具。展开后可逐个启停；禁用的工具不会暴露给模型。</source>
        <translation>Tools shipped with the app. Expand to toggle them individually; disabled tools are hidden from the model.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="86"/>
        <source>暂无</source>
        <translation>None</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="86"/>
        <location filename="../src/ui/pages/ToolsPage.qml" line="104"/>
        <source>无匹配</source>
        <translation>No matches</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="87"/>
        <location filename="../src/ui/pages/ToolsPage.qml" line="105"/>
        <source>%1 个 · 已启用 %2</source>
        <translation>%1 total · %2 enabled</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="100"/>
        <source>MCP 工具</source>
        <translation>MCP tools</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="101"/>
        <source>由已连接的 MCP 服务器提供，与内置工具分开管理。</source>
        <translation>Provided by connected MCP servers; managed separately from built-in tools.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="117"/>
        <source>暂无 MCP 工具。先到「MCP 服务器」页连接服务器，服务器提供的工具会出现在这里。</source>
        <translation>No MCP tools yet. Connect a server on the &quot;MCP servers&quot; page and its tools will appear here.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="131"/>
        <source>没有匹配「%1」的工具。换个关键词，或清空搜索查看全部。</source>
        <translation>No tools match “%1”. Try another keyword, or clear the search to see everything.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="190"/>
        <source>已配置</source>
        <translation>Configured</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="193"/>
        <source>输入新值可覆盖，留空不修改</source>
        <translation>Enter a new value to overwrite; leave blank to keep the current one</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="194"/>
        <source>输入搜索服务 API Key</source>
        <translation>Enter the search service API key</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="214"/>
        <source>测试中…</source>
        <translation>Testing…</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="235"/>
        <source>会话权限模式</source>
        <translation>Session Permission Mode</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="236"/>
        <source>新会话默认使用的工具审批级别。可在会话内随时通过输入框旁的模式选择器更改，仅对当前会话生效。</source>
        <translation>Default tool-approval level for new sessions. Change anytime per session via the mode selector next to the input box; applies to that session only.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="240"/>
        <source>默认模式</source>
        <translation>Default Mode</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="247"/>
        <source>变更确认</source>
        <translation>Confirm Changes</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="248"/>
        <source>只读（计划模式）</source>
        <translation>Read-only (plan mode)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="249"/>
        <source>自动编辑</source>
        <translation>Auto Edit</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="250"/>
        <source>完全访问</source>
        <translation>Full Access</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/ToolsPage.qml" line="260"/>
        <source>默认权限模式</source>
        <translation>Default Permission Mode</translation>
    </message>
</context>
<context>
    <name>TopBar</name>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="121"/>
        <source>收起侧边栏 (Ctrl+[)</source>
        <translation>Collapse Sidebar (Ctrl+[)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="121"/>
        <source>展开侧边栏 (Ctrl+[)</source>
        <translation>Expand Sidebar (Ctrl+[)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="175"/>
        <location filename="../src/ui/components/TopBar.qml" line="210"/>
        <source>选择项目</source>
        <translation>Select project</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="209"/>
        <source>当前项目：%1（点击更换）</source>
        <translation>Current project: %1 (click to change)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="211"/>
        <source>当前项目：%1</source>
        <translation>Current project: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="235"/>
        <source>无项目（本地对话）</source>
        <translation>No project (local chat)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="236"/>
        <source>普通对话，不关联任何文件夹</source>
        <translation>A plain chat, not linked to any folder</translation>
    </message>
    <message>
        <source>tokens: %1 / %2</source>
        <translation type="vanished">tokens: %1 / %2</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="442"/>
        <source>¥%1/轮</source>
        <translation>$%1/req</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="514"/>
        <location filename="../src/ui/components/TopBar.qml" line="570"/>
        <source>系统提示词</source>
        <translation>System Prompt</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="515"/>
        <source>工具定义</source>
        <translation>Tool Definitions</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="516"/>
        <location filename="../src/ui/components/TopBar.qml" line="571"/>
        <source>对话</source>
        <translation>Conversation</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="517"/>
        <source>工具结果</source>
        <translation>Tool Results</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="540"/>
        <source>会话累计 · %1 tokens · 命中率 %2%</source>
        <translation>Session total · %1 tokens · hit rate %2%</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="551"/>
        <source>输入（未命中）</source>
        <translation>Input (cache miss)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="553"/>
        <source>输出</source>
        <translation>Output</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="574"/>
        <source>技能</source>
        <translation>Skills</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="611"/>
        <source>上下文构成 · %1 / %2 · %3%</source>
        <translation>Context · %1 / %2 · %3%</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="614"/>
        <source>上下文构成 · %1</source>
        <translation>Context · %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="702"/>
        <source>剩余可用</source>
        <translation>Free</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="827"/>
        <source>本会话累计 %1 次请求 · ¥%2</source>
        <translation>Session total: %1 requests · $%2</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="830"/>
        <source>本会话累计 %1 次请求</source>
        <translation>Session total: %1 requests</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="887"/>
        <source>任务清单（%1 项未完成）</source>
        <translation>Task list (%1 unfinished)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="888"/>
        <source>打开任务清单，%1 项未完成</source>
        <translation>Open the task list, %1 unfinished</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="943"/>
        <source>审阅本次会话的文件改动（%1 个文件待查看）</source>
        <translation>Review this session&apos;s file changes (%1 files to look at)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="944"/>
        <source>审阅本次会话的文件改动 (Ctrl+R)</source>
        <translation>Review this session&apos;s file changes (Ctrl+R)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="946"/>
        <source>打开文件改动审阅，%1 个文件待查看</source>
        <translation>Open the file-change review, %1 files to look at</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="992"/>
        <source>收起右侧面板 (Ctrl+])</source>
        <translation>Collapse right panel (Ctrl+])</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="992"/>
        <source>展开右侧面板 (Ctrl+])</source>
        <translation>Expand right panel (Ctrl+])</translation>
    </message>
    <message>
        <source>任务清单（%1\ 项未完成）</source>
        <translation type="vanished">Task list (%1 unfinished)</translation>
    </message>
    <message>
        <source>打开任务清单，%1\ 项未完成</source>
        <translation type="vanished">Open the task list, %1 unfinished</translation>
    </message>
    <message>
        <source>审阅本次会话的文件改动\ \(Ctrl\+R\)</source>
        <translation type="vanished">Review this session&apos;s file changes (Ctrl+R)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="947"/>
        <source>打开文件改动审阅</source>
        <translation>Open the file change review</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="572"/>
        <source>工具调用</source>
        <translation>Tool Calls</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="335"/>
        <source>新会话</source>
        <translation>New Session</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="336"/>
        <source>新对话</source>
        <translation>New Chat</translation>
    </message>
    <message>
        <source>从会话模板开始</source>
        <translation type="vanished">Start from a session template</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="573"/>
        <source>MCP</source>
        <translation>MCP</translation>
    </message>
    <message>
        <source>Skill</source>
        <translation type="vanished">Skill</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="518"/>
        <location filename="../src/ui/components/TopBar.qml" line="575"/>
        <source>图片</source>
        <translation>Images</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="576"/>
        <source>其他</source>
        <translation>Other</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="601"/>
        <source>暂无数据</source>
        <translation>No data yet</translation>
    </message>
    <message>
        <source>会话 token 细节 · %1 次请求</source>
        <translation type="vanished">Session token details · %1 requests</translation>
    </message>
    <message>
        <source>输入 %1</source>
        <translation type="vanished">Input %1</translation>
    </message>
    <message>
        <source>输出 %1</source>
        <translation type="vanished">Output %1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="552"/>
        <source>缓存命中</source>
        <translation>Cache Hit</translation>
    </message>
    <message>
        <source>合计</source>
        <translation type="vanished">Total</translation>
    </message>
    <message>
        <source>收起上下文面板 (Ctrl+B)</source>
        <translation type="vanished">Collapse Context Panel (Ctrl+B)</translation>
    </message>
    <message>
        <source>展开上下文面板 (Ctrl+B)</source>
        <translation type="vanished">Expand Context Panel (Ctrl+B)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="857"/>
        <source>折叠思考与工具调用过程（%1 项）</source>
        <translation>Collapse thinking and tool-call process (%1)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/TopBar.qml" line="858"/>
        <source>显示思考与工具调用过程（%1 项）</source>
        <translation>Show thinking and tool-call process (%1)</translation>
    </message>
</context>
<context>
    <name>UsageDetailTable</name>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="39"/>
        <source>时间</source>
        <translation>Time</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="40"/>
        <source>模型</source>
        <translation>Model</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="41"/>
        <source>总 Token</source>
        <translation>Total Tokens</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="42"/>
        <source>输入</source>
        <translation>Input</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="43"/>
        <source>输出</source>
        <translation>Output</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="44"/>
        <source>缓存占比</source>
        <translation>Cache Ratio</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="45"/>
        <source>延迟</source>
        <translation>Latency</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="46"/>
        <source>费用</source>
        <translation>Cost</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="91"/>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="266"/>
        <source>全部模型</source>
        <translation>All models</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="140"/>
        <source>已加载 %1 / 共 %2 条</source>
        <translation>%1 of %2 loaded</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="143"/>
        <source> · 筛选后 %1 条</source>
        <translation> · %1 after filtering</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="152"/>
        <source>¥%1</source>
        <translation>$%1</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="155"/>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="160"/>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="166"/>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="173"/>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="185"/>
        <source>—</source>
        <translation>—</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="156"/>
        <source>%1 ms</source>
        <translation>%1 ms</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="157"/>
        <source>%1 s</source>
        <translation>%1 s</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="161"/>
        <source>%1%</source>
        <translation>%1%</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="170"/>
        <source>(未知)</source>
        <translation>(unknown)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="241"/>
        <source>搜索模型 / 时间…</source>
        <translation>Search model / time…</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="256"/>
        <source>搜索请求明细（模型或时间）</source>
        <translation>Search request details (model or time)</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="272"/>
        <source>按模型筛选请求明细</source>
        <translation>Filter request details by model</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="276"/>
        <source>重置</source>
        <translation>Reset</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="280"/>
        <source>重置筛选与排序</source>
        <translation>Reset filters and sorting</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="426"/>
        <source>请求明细表格</source>
        <translation>Request details table</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="506"/>
        <source>该范围内暂无数据</source>
        <translation>No data in this range</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="507"/>
        <source>没有匹配的请求</source>
        <translation>No matching requests</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="542"/>
        <source>重置列宽</source>
        <translation>Reset column widths</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="544"/>
        <source>恢复默认列宽</source>
        <translation>Restore default column widths</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="548"/>
        <source>加载更多</source>
        <translation>Load More</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UsageDetailTable.qml" line="550"/>
        <source>加载更多请求明细</source>
        <translation>Load more request details</translation>
    </message>
</context>
<context>
    <name>UsageStatsPage</name>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="21"/>
        <source>用量统计页</source>
        <translation>Usage Statistics Page</translation>
    </message>
    <message>
        <source>查看模型调用次数、Token 消耗与延迟统计</source>
        <translation type="vanished">View model call counts, token usage and latency statistics</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="239"/>
        <source>—</source>
        <translation>—</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="240"/>
        <source>%1 ms</source>
        <translation>%1 ms</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="241"/>
        <source>%1 s</source>
        <translation>%1 s</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="287"/>
        <source>用量统计</source>
        <translation>Usage Statistics</translation>
    </message>
    <message>
        <source>统计已完成任务的模型调用次数、Token 消耗与平均延迟。</source>
        <translation type="vanished">Statistics on model call counts, token usage and average latency for completed tasks.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="326"/>
        <source>统计范围</source>
        <translation>Time Range</translation>
    </message>
    <message>
        <source>近 7 天</source>
        <translation type="vanished">Last 7 days</translation>
    </message>
    <message>
        <source>近 30 天</source>
        <translation type="vanished">Last 30 days</translation>
    </message>
    <message>
        <source>近 90 天</source>
        <translation type="vanished">Last 90 days</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="337"/>
        <source>刷新</source>
        <translation>Refresh</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="339"/>
        <source>刷新用量统计</source>
        <translation>Refresh usage statistics</translation>
    </message>
    <message>
        <source>日</source>
        <translation type="vanished">Day</translation>
    </message>
    <message>
        <source>月</source>
        <translation type="vanished">Month</translation>
    </message>
    <message>
        <source>日¥</source>
        <translation type="vanished">Day ¥</translation>
    </message>
    <message>
        <source>月¥</source>
        <translation type="vanished">Month ¥</translation>
    </message>
    <message>
        <source>超限拒绝</source>
        <translation type="vanished">Hard-stop at limit</translation>
    </message>
    <message>
        <source>达到上限时直接拒绝请求（默认仅警告）</source>
        <translation type="vanished">Reject requests once the limit is reached (warn-only by default)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1223"/>
        <source>超限时直接拒绝请求</source>
        <translation>Reject requests when quota exceeded</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="482"/>
        <source>任务次数</source>
        <translation>Task Count</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="483"/>
        <source>累计 Token</source>
        <translation>Total Tokens</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="485"/>
        <source>平均延迟</source>
        <translation>Avg Latency</translation>
    </message>
    <message>
        <source>按模型</source>
        <translation type="vanished">By Model</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="801"/>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="920"/>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1115"/>
        <source>该范围内暂无数据</source>
        <translation>No data in this range</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="89"/>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="936"/>
        <source>(未知)</source>
        <translation>(unknown)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="22"/>
        <source>查看模型调用次数、Token 消耗、费用与配额统计</source>
        <translation>View model call counts, token usage, cost and quota statistics</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="167"/>
        <source>没有可导出的明细。</source>
        <translation>No details to export.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="196"/>
        <source>日期格式应为 YYYY-MM-DD</source>
        <translation>Date format should be YYYY-MM-DD</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="200"/>
        <source>起始日期不能晚于截止日期</source>
        <translation>Start date cannot be later than end date</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="246"/>
        <source>¥%1</source>
        <translation>$%1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="250"/>
        <source>%1%</source>
        <translation>%1%</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="256"/>
        <source>系统提示词</source>
        <translation>System Prompt</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="257"/>
        <source>对话消息</source>
        <translation>Conversation</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="258"/>
        <source>工具调用</source>
        <translation>Tool Calls</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="259"/>
        <source>MCP</source>
        <translation>MCP</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="260"/>
        <source>技能</source>
        <translation>Skills</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="261"/>
        <source>图片</source>
        <translation>Images</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="262"/>
        <source>其他</source>
        <translation>Other</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="264"/>
        <source>未知</source>
        <translation>Unknown</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="288"/>
        <source>统计已完成任务的模型调用次数、Token 消耗、费用与配额状态。</source>
        <translation>Statistics of completed task model calls, token usage, cost and quota status.</translation>
    </message>
    <message>
        <source>配额</source>
        <translation type="vanished">Quotas</translation>
    </message>
    <message>
        <source>0 = 不限制。费用按已计费请求累计（本地模型不计费）。勾选“超限拒绝”后达到上限直接拒绝请求，默认仅警告。</source>
        <translation type="vanished">0 = unlimited. Cost accrues from billed requests only (local models are free). With hard-stop enabled, requests are rejected once the limit is reached; warn-only by default.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1198"/>
        <source>配额设置</source>
        <translation>Quota Settings</translation>
    </message>
    <message>
        <source>超限时直接拒绝请求（默认仅警告）</source>
        <translation type="vanished">Reject requests when exceeded (warn only by default)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1264"/>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1272"/>
        <source>每日 Token 上限</source>
        <translation>Daily Token Limit</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1274"/>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1282"/>
        <source>每月 Token 上限</source>
        <translation>Monthly Token Limit</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1284"/>
        <source>每日费用上限（元）</source>
        <translation>Daily cost limit (CNY)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1292"/>
        <source>每日费用上限 元</source>
        <translation>Daily Cost Limit (CNY)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1294"/>
        <source>每月费用上限（元）</source>
        <translation>Monthly cost limit (CNY)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1302"/>
        <source>每月费用上限 元</source>
        <translation>Monthly Cost Limit (CNY)</translation>
    </message>
    <message>
        <source>0 = 不限制。费用按已计费请求累计（本地模型不计费）。</source>
        <translation type="vanished">0 = unlimited. Cost accrues from billed requests only (local models are free).</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1205"/>
        <source>0 = 不限制 · 本地模型不计费</source>
        <translation>0 = unlimited · local models are free</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1214"/>
        <source>费用按已计费请求累计。勾选“超限拒绝”后达到上限直接拒绝请求，默认仅警告。</source>
        <translation>Cost accrues from billed requests only. With hard-stop enabled, requests are rejected once the limit is reached; warn-only by default.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1220"/>
        <source>超限拒绝请求</source>
        <translation>Reject at limit</translation>
    </message>
    <message>
        <source>浏览操作与网络请求的使用轨迹。仅记录操作元数据（操作类型、对象、结果），不包含对话内容、文档内容与 API 密钥。</source>
        <translation type="vanished">Browse the usage trail of operations and network requests. Only operation metadata (action, target, result) is recorded; conversation content, document content and API keys are never included.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="484"/>
        <source>累计费用</source>
        <translation>Total Cost</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="649"/>
        <source>Token 构成</source>
        <translation>Token Composition</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="773"/>
        <source>输入</source>
        <translation>Input</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="675"/>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="781"/>
        <source>输出</source>
        <translation>Output</translation>
    </message>
    <message>
        <source>用户</source>
        <translation type="vanished">User</translation>
    </message>
    <message>
        <source>智能体</source>
        <translation type="vanished">Agent</translation>
    </message>
    <message>
        <source>网络</source>
        <translation type="vanished">Network</translation>
    </message>
    <message>
        <source>活动记录</source>
        <translation type="vanished">Activity Log</translation>
    </message>
    <message>
        <source>浏览操作与网络请求的使用轨迹。仅记录操作元数据（操作类型、对象、结果），不包含对话内容与文档内容。</source>
        <translation type="vanished">Browse the usage trail of operations and network requests. Only operation metadata (action, target, result) is recorded; conversation and document contents are never included.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="352"/>
        <source>全部</source>
        <translation>All</translation>
    </message>
    <message>
        <source>用户操作</source>
        <translation type="vanished">User Actions</translation>
    </message>
    <message>
        <source>活动类别筛选</source>
        <translation type="vanished">Activity category filter</translation>
    </message>
    <message>
        <source>过滤操作 / 对象 / 说明…</source>
        <translation type="vanished">Filter by action / target / detail…</translation>
    </message>
    <message>
        <source>过滤活动记录关键词</source>
        <translation type="vanished">Filter activity log by keyword</translation>
    </message>
    <message>
        <source>共 %1 条</source>
        <translation type="vanished">%1 entries total</translation>
    </message>
    <message>
        <source>刷新活动记录</source>
        <translation type="vanished">Refresh activity log</translation>
    </message>
    <message>
        <source>暂无活动记录</source>
        <translation type="vanished">No activity records yet</translation>
    </message>
    <message>
        <source>无匹配的活动记录</source>
        <translation type="vanished">No matching activities</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="349"/>
        <source>今日</source>
        <translation>Today</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="350"/>
        <source>7天内</source>
        <translation>Last 7 days</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="351"/>
        <source>30天内</source>
        <translation>Last 30 days</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="353"/>
        <source>自定义</source>
        <translation>Custom</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="378"/>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="400"/>
        <source>起始日期</source>
        <translation>Start date</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="405"/>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="427"/>
        <source>截止日期（含当天）</source>
        <translation>End date (inclusive)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="438"/>
        <source>今天</source>
        <translation>Today</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="443"/>
        <source>快捷选择今天</source>
        <translation>Quick select today</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="446"/>
        <source>昨天</source>
        <translation>Yesterday</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="452"/>
        <source>快捷选择昨天</source>
        <translation>Quick select yesterday</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="455"/>
        <source>本月</source>
        <translation>This month</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="462"/>
        <source>快捷选择本月</source>
        <translation>Quick select this month</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="673"/>
        <source>输入(未命中)</source>
        <translation>Input (cache miss)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="674"/>
        <source>缓存命中</source>
        <translation>Cache Hit</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="723"/>
        <source>缓存命中率: %1</source>
        <translation>Cache hit rate: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="738"/>
        <source>按来源拆分</source>
        <translation>By Source</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="765"/>
        <source>来源</source>
        <translation>Source</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="789"/>
        <source>占比</source>
        <translation>Share</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="857"/>
        <source>费用明细</source>
        <translation>Cost Details</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="884"/>
        <source>模型</source>
        <translation>Model</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="892"/>
        <source>调用</source>
        <translation>Calls</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1070"/>
        <source>导出表格当前视图（已加载 %1 / 共 %2 行）</source>
        <translation>Export the current table view (%1 of %2 rows loaded)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1307"/>
        <source>告警阈值（%）</source>
        <translation>Alert threshold (%)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1315"/>
        <source>用量达到配额的这个百分比时提醒一次，0 = 关闭。仅在未超限时提醒。</source>
        <translation>Notify once when usage reaches this percentage of the quota; 0 turns it off. Only fires before the limit is exceeded.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1324"/>
        <source>用量告警阈值 百分比</source>
        <translation>Usage alert threshold percentage</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1337"/>
        <source>导出请求明细</source>
        <translation>Export request details</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1340"/>
        <source>CSV 文件 (*.csv)</source>
        <translation>CSV files (*.csv)</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1347"/>
        <source>已导出 %1 行明细。</source>
        <translation>Exported %1 rows.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1348"/>
        <source>导出失败，请检查目标路径是否可写。</source>
        <translation>Export failed; check that the destination path is writable.</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="621"/>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="900"/>
        <source>Token</source>
        <translation>Token</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="908"/>
        <source>费用</source>
        <translation>Cost</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="980"/>
        <source>合计</source>
        <translation>Total</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1013"/>
        <source>预估月费用: %1</source>
        <translation>Projected monthly cost: %1</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1043"/>
        <source>点击表头排序 · 拖动列边界调整列宽</source>
        <translation>Click a header to sort · drag column edges to resize</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1063"/>
        <source>导出 CSV</source>
        <translation>Export CSV</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1066"/>
        <source>将请求明细导出为 CSV 文件</source>
        <translation>Export request details as a CSV file</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1090"/>
        <source>配额状态</source>
        <translation>Quota Status</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1125"/>
        <source>日 Token</source>
        <translation>Daily Tokens</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1126"/>
        <source>日费用</source>
        <translation>Daily Cost</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1127"/>
        <source>月 Token</source>
        <translation>Monthly Tokens</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1128"/>
        <source>月费用</source>
        <translation>Monthly Cost</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1145"/>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1147"/>
        <source>%1 / %2</source>
        <translation>%1 / %2</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1146"/>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1148"/>
        <source>不限</source>
        <translation>unlimited</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="531"/>
        <source>用量图表</source>
        <translation>Usage Charts</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="553"/>
        <source>模型 Token 分布</source>
        <translation>Model Token Distribution</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="562"/>
        <source>总 Token</source>
        <translation>Total Tokens</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="582"/>
        <source>按天走势</source>
        <translation>Daily Trend</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="614"/>
        <source>模型对比</source>
        <translation>Model Comparison</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="624"/>
        <source>按 Token 对比</source>
        <translation>Compare by tokens</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="627"/>
        <source>调用次数</source>
        <translation>Calls</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="630"/>
        <source>按调用次数对比</source>
        <translation>Compare by calls</translation>
    </message>
    <message>
        <source>%1 次</source>
        <translation type="vanished">%1 calls</translation>
    </message>
    <message>
        <source>%1 tokens</source>
        <translation type="vanished">%1 tokens</translation>
    </message>
    <message>
        <source>按天</source>
        <translation type="vanished">By Day</translation>
    </message>
    <message>
        <location filename="../src/ui/pages/UsageStatsPage.qml" line="1035"/>
        <source>请求明细</source>
        <translation>Request Details</translation>
    </message>
    <message>
        <source>时间</source>
        <translation type="vanished">Time</translation>
    </message>
    <message>
        <source>缓存占比</source>
        <translation type="vanished">Cache Ratio</translation>
    </message>
    <message>
        <source>延迟</source>
        <translation type="vanished">Latency</translation>
    </message>
    <message>
        <source>已显示 %1 / 共 %2 条</source>
        <translation type="vanished">Showing %1 of %2 records</translation>
    </message>
    <message>
        <source>加载更多</source>
        <translation type="vanished">Load More</translation>
    </message>
    <message>
        <source>加载更多明细</source>
        <translation type="vanished">Load more details</translation>
    </message>
</context>
<context>
    <name>UserMessageBubble</name>
    <message>
        <location filename="../src/ui/components/UserMessageBubble.qml" line="105"/>
        <source>发送</source>
        <translation>Send</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UserMessageBubble.qml" line="118"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <source>分支 %1/%2</source>
        <translation type="vanished">Branch %1/%2</translation>
    </message>
    <message>
        <source>查看该处的 %1 条分支</source>
        <translation type="vanished">Show the %1 branches at this point</translation>
    </message>
    <message>
        <source>从此处分支</source>
        <translation type="vanished">Branch from here</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UserMessageBubble.qml" line="199"/>
        <source>编辑</source>
        <translation>Edit</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UserMessageBubble.qml" line="228"/>
        <source>编辑并重发</source>
        <translation>Edit and resend</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UserMessageBubble.qml" line="229"/>
        <source>修改这条消息并重新发送</source>
        <translation>Edit this message and send it again</translation>
    </message>
    <message>
        <source>（空）</source>
        <translation type="vanished">(empty)</translation>
    </message>
    <message>
        <source> · 当前</source>
        <translation type="vanished"> · current</translation>
    </message>
    <message>
        <source>确认删除</source>
        <translation type="vanished">Confirm delete</translation>
    </message>
    <message>
        <source>删除此分支</source>
        <translation type="vanished">Delete this branch</translation>
    </message>
    <message>
        <location filename="../src/ui/components/UserMessageBubble.qml" line="238"/>
        <source>你的消息</source>
        <translation>Your message</translation>
    </message>
</context>
<context>
    <name>main</name>
    <message>
        <source>收起侧边栏 (Ctrl+[)</source>
        <translation type="vanished">Collapse Sidebar (Ctrl+[)</translation>
    </message>
    <message>
        <source>展开侧边栏 (Ctrl+[)</source>
        <translation type="vanished">Expand Sidebar (Ctrl+[)</translation>
    </message>
    <message>
        <source>收起上下文面板 (Ctrl+B)</source>
        <translation type="vanished">Collapse Context Panel (Ctrl+B)</translation>
    </message>
    <message>
        <source>展开上下文面板 (Ctrl+B)</source>
        <translation type="vanished">Expand Context Panel (Ctrl+B)</translation>
    </message>
    <message>
        <source>设置已保存</source>
        <translation type="vanished">Settings saved</translation>
    </message>
    <message>
        <location filename="../src/ui/main.qml" line="320"/>
        <source>已删除会话「%1」，可撤销</source>
        <translation>Session &quot;%1&quot; deleted — undo available</translation>
    </message>
    <message>
        <location filename="../src/ui/main.qml" line="321"/>
        <source>会话</source>
        <translation>session</translation>
    </message>
    <message>
        <location filename="../src/ui/main.qml" line="322"/>
        <source>撤销</source>
        <translation>Undo</translation>
    </message>
</context>
</TS>
