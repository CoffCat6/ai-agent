import QtQuick

// Vector icon painter: each icon is drawn from path primitives on a 24x24
// grid and scaled to `size`. Canvas (software raster) is used deliberately:
// the Shapes/GeometryRenderer path renders nothing when Qt Quick falls back
// to the software graphics backend, while Canvas works everywhere.
// Repaints are wired to every input so theme switches and hover recolors
// stay live.
//
// Usage: AppIcon { icon: "sidebar-collapse"; size: 20; iconColor: Theme.textPrimary }
// 未注册的 icon 名回退到 painters["menu"]（三条横线），见下面的 def 绑定。
Canvas {
    id: root

    property string icon: "menu"
    property color iconColor: Theme.textPrimary
    property real size: 20

    readonly property var def: painters[icon] !== undefined ? painters[icon] : painters["menu"]

    implicitWidth: size
    implicitHeight: size
    width: size
    height: size
    antialiasing: true

    onIconChanged: requestPaint()
    onIconColorChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    Component.onCompleted: requestPaint()

    // ── Primitive helpers ──────────────────────────────
    function line(c, x1, y1, x2, y2) {
        c.beginPath(); c.moveTo(x1, y1); c.lineTo(x2, y2); c.stroke()
    }
    function polyline(c, pts) {
        c.beginPath(); c.moveTo(pts[0], pts[1])
        for (var i = 2; i < pts.length; i += 2) c.lineTo(pts[i], pts[i + 1])
        c.stroke()
    }
    function circle(c, cx, cy, r) {
        c.beginPath(); c.arc(cx, cy, r, 0, 2 * Math.PI, false); c.stroke()
    }
    function roundedRect(c, x, y, w, h, r) {
        c.beginPath()
        c.moveTo(x + r, y)
        c.lineTo(x + w - r, y); c.quadraticCurveTo(x + w, y, x + w, y + r)
        c.lineTo(x + w, y + h - r); c.quadraticCurveTo(x + w, y + h, x + w - r, y + h)
        c.lineTo(x + r, y + h); c.quadraticCurveTo(x, y + h, x, y + h - r)
        c.lineTo(x, y + r); c.quadraticCurveTo(x, y, x + r, y)
        c.stroke()
    }
    function filled(c, closer) {
        c.beginPath(); closer(c); c.closePath()
        c.fill(); c.stroke()
    }

    // ── Icon painters (24x24 grid) ─────────────────────
    readonly property var painters: ({
        // 未知 icon 的兜底画笔（def 绑定会回退到它），本身不再被任何界面
        // 直接引用——侧栏开关已改用下面的 sidebar-collapse / sidebar-expand。
        "menu": function(c) {
            line(c, 4, 6.5, 20, 6.5); line(c, 4, 12, 20, 12); line(c, 4, 17.5, 20, 17.5)
        },
        // docs/35 P4.3: 后台子代理入口。机器人头像（天线 + 圆角头 + 双眼 + 嘴）
// 表达「AI 代理」；此前用的三节点+连线与 Git 分支图几乎同形，放在
// 任务清单/审阅旁边会被误读成版本管理入口。
        "agents": function(c) {
            // 天线与信号点。
            line(c, 12, 2.6, 12, 5.1)
            c.beginPath(); c.arc(12, 2.4, 1.15, 0, 2 * Math.PI, false); c.fill()
            // 头部轮廓。
            roundedRect(c, 4.2, 5.1, 15.6, 12.5, 3.4)
            // 两侧耳罩：让 16px 下的轮廓也明显是"机器人"，而不是显示器。
            line(c, 2.7, 9.6, 2.7, 13.2)
            line(c, 21.3, 9.6, 21.3, 13.2)
            // 双眼（实心，16px 下仍能看出两个点）。
            c.beginPath(); c.arc(8.9, 10.4, 1.45, 0, 2 * Math.PI, false); c.fill()
            c.beginPath(); c.arc(15.1, 10.4, 1.45, 0, 2 * Math.PI, false); c.fill()
            // 嘴。
            line(c, 9.6, 14.5, 14.4, 14.5)
        },
        // 侧栏开合（TopBar 唯一的侧栏按钮）。三横线 "menu" 表达的是"菜单"，
        // 与"收起/展开一个面板"毫无关系；这里换成"面板 + 指向它的箭头"：
        //   · 外框 = 整个窗口轮廓，左侧竖线 = 侧栏分割线（靠左，因为侧栏在左）
        //   · 左半阴影块 = 侧栏本体，开着时填实、收起时留空
        //   · 箭头指向侧栏 = "展开"，背离 = "收起"
        // 用两个条目而不是靠 icon 名传参：AppIcon 的画笔签名只有 ctx，
        // 想按状态变形只能各画一版（chevron-left/right 也是这个做法）。
        "sidebar-collapse": function(c) {
            // 窗口轮廓 + 侧栏分隔线。
            roundedRect(c, 3.5, 5, 17, 14, 2)
            line(c, 9.5, 5, 9.5, 19)
            // 侧栏本体填实：强调"它现在占着位置"。
            c.beginPath()
            c.moveTo(4.8, 6.2); c.lineTo(9.5, 6.2)
            c.lineTo(9.5, 17.8); c.lineTo(4.8, 17.8)
            c.closePath(); c.fill()
            // 向内的箭头：收起。
            polyline(c, [17.5, 9.5, 14, 12, 17.5, 14.5])
        },
        "sidebar-expand": function(c) {
            roundedRect(c, 3.5, 5, 17, 14, 2)
            line(c, 9.5, 5, 9.5, 19)
            // 收起态不填侧栏本体：留空就是"这里没有内容"。
            // 向外的箭头：展开。
            polyline(c, [14, 9.5, 17.5, 12, 14, 14.5])
        },
        // 右侧上下文面板开合。与上面的侧栏图标**同一套视觉语法**（窗口轮廓
        // + 面板分隔线 + 面板本体填实/留空 + 指向箭头），只是镜像到右边：
        //   · 分隔线在右（x=14.5），填实块落在右半
        //   · 箭头在左半：指向右 = 展开，指向左 = 收起
        // 两侧共用同一套语义，用户学会一个就学会另一个；此前右侧用的是裸
        // chevron，和左侧的"面板+箭头"完全不同构，看过去像两个不相干的功能。
        "panel-collapse": function(c) {
            roundedRect(c, 3.5, 5, 17, 14, 2)
            line(c, 14.5, 5, 14.5, 19)
            // 面板本体填实：强调"它现在占着位置"。
            c.beginPath()
            c.moveTo(14.5, 6.2); c.lineTo(19.2, 6.2)
            c.lineTo(19.2, 17.8); c.lineTo(14.5, 17.8)
            c.closePath(); c.fill()
            // 向内的箭头（指向面板）：收起。
            polyline(c, [6.5, 9.5, 10, 12, 6.5, 14.5])
        },
        "panel-expand": function(c) {
            roundedRect(c, 3.5, 5, 17, 14, 2)
            line(c, 14.5, 5, 14.5, 19)
            // 收起态不填面板本体。
            // 向外的箭头（指向面板要出现的方向）：展开。
            polyline(c, [10, 9.5, 13.5, 12, 10, 14.5])
        },
        // 过程节点的显示/折叠。与 sidebar-/panel- 同族（同一套"轮廓 + 指向
        // 箭头"语法），但表达的不是面板而是"一叠内容行"：
        //   · 三条内容行 = 思考/工具调用/工具结果这类过程节点
        //   · 展开态画满三行 + 向外的箭头；折叠态中间一行留空 + 向内的箭头
        // 这样三个开关放在 TopBar 同一排时，用户能看出它们是同一类控件。
        "process-expand": function(c) {
            // 三行内容（过程节点），自上而下。
            line(c, 3.5, 6, 17, 6)
            line(c, 3.5, 12, 17, 12)
            line(c, 3.5, 18, 17, 18)
            // 向外的箭头：展开。
            polyline(c, [18.5, 9, 21, 12, 18.5, 15])
        },
        "process-collapse": function(c) {
            line(c, 3.5, 6, 17, 6)
            // 中间一行留空：折叠态就是"中间的细节被收起来了"。
            line(c, 3.5, 18, 17, 18)
            // 向内的箭头：收起。
            polyline(c, [21, 9, 18.5, 12, 21, 15])
        },
        "close": function(c) {
            line(c, 6.5, 6.5, 17.5, 17.5); line(c, 17.5, 6.5, 6.5, 17.5)
        },
        "add": function(c) {
            line(c, 12, 5.5, 12, 18.5); line(c, 5.5, 12, 18.5, 12)
        },
        "image": function(c) {
            roundedRect(c, 4, 5, 16, 14, 2)
            polyline(c, [4.5, 15.5, 9, 11.5, 12.5, 14.5])
            polyline(c, [11.5, 14, 16, 9.5, 19.5, 12.5])
            circle(c, 9, 8.5, 1.2)
        },
        "chevron-left": function(c) { polyline(c, [14.5, 5.5, 8, 12, 14.5, 18.5]) },
        "chevron-right": function(c) { polyline(c, [9.5, 5.5, 16, 12, 9.5, 18.5]) },
        "chevron-up": function(c) { polyline(c, [5.5, 14.5, 12, 8, 18.5, 14.5]) },
        "chevron-down": function(c) { polyline(c, [5.5, 9.5, 12, 16, 18.5, 9.5]) },
        "edit": function(c) {
            polyline(c, [4.8, 19.2, 5.7, 15.8, 16, 5.5, 18.5, 8, 8.2, 18.3, 4.8, 19.2])
            line(c, 14.4, 7.1, 16.9, 9.6)
        },
        "terminal": function(c) {
            c.lineWidth = 1.6
            c.strokeStyle = c.fillStyle
            c.beginPath()
            c.rect(3, 4, 18, 16)
            c.stroke()
            c.beginPath()
            c.moveTo(7, 9); c.lineTo(10.5, 12); c.lineTo(7, 15)
            c.moveTo(12.5, 15); c.lineTo(17, 15)
            c.stroke()
        },
        "shield": function(c) {
            filled(c, function(k) {
                k.moveTo(12, 3.5); k.lineTo(18.5, 6); k.lineTo(18.5, 11.2)
                k.bezierCurveTo(18.5, 15.8, 15.9, 19.2, 12, 20.5)
                k.bezierCurveTo(8.1, 19.2, 5.5, 15.8, 5.5, 11.2)
                k.lineTo(5.5, 6)
            })
        },
        "settings": function(c) {
            line(c, 4, 7, 11.7, 7); line(c, 16.3, 7, 20, 7)
            line(c, 4, 12, 6.2, 12); line(c, 10.8, 12, 20, 12)
            line(c, 4, 17, 13.7, 17); line(c, 18.3, 17, 20, 17)
            circle(c, 14, 7, 2.3); circle(c, 8.5, 12, 2.3); circle(c, 16, 17, 2.3)
        },
        "moon": function(c) {
            c.beginPath()
            c.arc(12, 12, 7.07, -Math.PI / 3, Math.PI / 3, true)
            c.arc(15.5, 12, 7, Math.PI / 2, -Math.PI / 2, false)
            c.closePath(); c.stroke()
        },
        "sun": function(c) {
            circle(c, 12, 12, 4)
            for (var i = 0; i < 8; ++i) {
                var a = i * Math.PI / 4
                line(c, 12 + 6.2 * Math.cos(a), 12 + 6.2 * Math.sin(a),
                        12 + 8.8 * Math.cos(a), 12 + 8.8 * Math.sin(a))
            }
        },
        "trash": function(c) {
            line(c, 4.5, 6.5, 19.5, 6.5)
            c.beginPath()
            c.moveTo(9.5, 6.5); c.lineTo(9.5, 5.3)
            c.quadraticCurveTo(9.5, 3.8, 11, 3.8); c.lineTo(13, 3.8)
            c.quadraticCurveTo(14.5, 3.8, 14.5, 5.3); c.lineTo(14.5, 6.5)
            c.stroke()
            c.beginPath()
            c.moveTo(6.6, 6.8); c.lineTo(7.15, 17.8)
            c.quadraticCurveTo(7.25, 19.9, 9.3, 19.9); c.lineTo(14.7, 19.9)
            c.quadraticCurveTo(16.75, 19.9, 16.85, 17.8); c.lineTo(17.4, 6.8)
            c.stroke()
            line(c, 10, 10.5, 10, 16.5); line(c, 14, 10.5, 14, 16.5)
        },
        "refresh": function(c) {
            c.beginPath(); c.arc(12, 12, 7.5, -Math.PI / 4, Math.PI * 1.1, false); c.stroke()
            line(c, 17.3, 6.7, 17.3, 3.2); line(c, 17.3, 6.7, 20.8, 6.7)
        },
        "check": function(c) { polyline(c, [5, 12.5, 9.7, 17.2, 19, 7.6]) },
        "warning": function(c) {
            polyline(c, [12, 4.2, 21.2, 19.8, 2.8, 19.8, 12, 4.2])
            line(c, 12, 10, 12, 14.2)
            line(c, 12, 17, 12, 17.02)   // round cap renders the dot
        },
        "copy": function(c) {
            c.beginPath()
            c.moveTo(9.5, 8.5); c.lineTo(9.5, 6)
            c.quadraticCurveTo(9.5, 4.2, 11.3, 4.2); c.lineTo(18.2, 4.2)
            c.quadraticCurveTo(20, 4.2, 20, 6); c.lineTo(20, 12.7)
            c.quadraticCurveTo(20, 14.5, 18.2, 14.5); c.lineTo(15.3, 14.5)
            c.stroke()
            roundedRect(c, 4.2, 9.5, 11.1, 10.5, 1.8)
        },
        "folder": function(c) {
            c.beginPath()
            c.moveTo(3.5, 19); c.lineTo(3.5, 6.5)
            c.quadraticCurveTo(3.5, 5, 5, 5); c.lineTo(9.2, 5); c.lineTo(11.2, 7.5)
            c.lineTo(19, 7.5); c.quadraticCurveTo(20.5, 7.5, 20.5, 9)
            c.lineTo(20.5, 19); c.quadraticCurveTo(20.5, 20.5, 19, 20.5)
            c.lineTo(5, 20.5); c.quadraticCurveTo(3.5, 20.5, 3.5, 19)
            c.closePath(); c.stroke()
            line(c, 12, 10.5, 12, 16.5); line(c, 9, 13.5, 15, 13.5)
        },
        "file": function(c) {
            // 文档（Feather "file-text" 简化版）：折角页 + 两条正文线。
            c.beginPath()
            c.moveTo(6, 3.5); c.lineTo(14.5, 3.5); c.lineTo(19, 8)
            c.lineTo(19, 20.5); c.lineTo(6, 20.5); c.closePath()
            c.stroke()
            polyline(c, [14.5, 3.5, 14.5, 8, 19, 8])
            line(c, 9, 12.5, 16, 12.5); line(c, 9, 16, 14, 16)
        },
        "send": function(c) {
            c.lineWidth = 1
            filled(c, function(k) {
                k.moveTo(3.5, 20.5); k.lineTo(21, 12); k.lineTo(3.5, 3.5)
                k.lineTo(3.5, 10.3); k.lineTo(14.8, 12); k.lineTo(3.5, 13.7)
            })
        },
        "stop": function(c) {
            c.lineWidth = 1.6
            filled(c, function(k) {
                k.moveTo(9.4, 8.4); k.lineTo(15.6, 8.4)
                k.quadraticCurveTo(16.6, 8.4, 16.6, 9.4); k.lineTo(16.6, 15.6)
                k.quadraticCurveTo(16.6, 16.6, 15.6, 16.6); k.lineTo(9.4, 16.6)
                k.quadraticCurveTo(8.4, 16.6, 8.4, 15.6); k.lineTo(8.4, 9.4)
                k.quadraticCurveTo(8.4, 8.4, 9.4, 8.4)
            })
        },
        "search": function(c) {
            c.beginPath()
            c.arc(10.8, 10.8, 5.8, 0, 2 * Math.PI, false); c.stroke()
            line(c, 15.2, 15.2, 20, 20)
        },
        "export": function(c) {
            line(c, 12, 4, 12, 14.5)
            polyline(c, [8, 8.5, 12, 4.2, 16, 8.5])
            polyline(c, [5.5, 13.5, 5.5, 18.5, 18.5, 18.5, 18.5, 13.5])
        },
        "layers": function(c) {
            // 层叠（Feather "layers" 风格）：菱形顶 + 两道下弧，
            // 表示“累计堆叠”语义，用于 token 区的会话累计图标。
            polyline(c, [12, 3.5, 20.5, 8, 12, 12.5, 3.5, 8, 12, 3.5])
            polyline(c, [3.5, 12.5, 12, 17, 20.5, 12.5])
            polyline(c, [3.5, 16.5, 12, 21, 20.5, 16.5])
        },
        "preset": function(c) {
            // 角色面具（Feather 无同款，按 "persona/mask" 自绘）：面具轮廓 +
            // 两个眼孔。用于提示词预设——语义是"换一个说话的设定"，与
            // token 累计的 layers、附件的图片/文件图标都不撞车。
            c.beginPath()
            c.moveTo(4.2, 5.2)
            c.lineTo(19.8, 5.2)
            c.lineTo(19.8, 12.4)
            c.quadraticCurveTo(19.8, 19.4, 12, 21)
            c.quadraticCurveTo(4.2, 19.4, 4.2, 12.4)
            c.closePath()
            c.stroke()
            line(c, 8.6, 10, 8.6, 12)      // 眼孔（竖短线）
            line(c, 15.4, 10, 15.4, 12)
        },
        "pin": function(c) {
            // 图钉（Feather "bookmark" 语义更适合侧栏置顶：书签钉在会话上）。
            polyline(c, [7, 3.5, 17, 3.5, 17, 20.5, 12, 16, 7, 20.5, 7, 3.5])
        },
        "archive": function(c) {
            // 归档箱（Feather "archive"）：顶盖 + 箱体 + 中缝提手。
            roundedRect(c, 3.5, 4, 17, 4.5, 1)
            polyline(c, [5.5, 8.5, 5.5, 19, 18.5, 19, 18.5, 8.5])
            line(c, 9.8, 12.5, 14.2, 12.5)
        },
        "undo": function(c) {
            // 回退箭头（Feather "corner-up-left"）：审阅树的单文件撤销。
            polyline(c, [9, 14.5, 4.5, 10, 9, 5.5])
            c.beginPath()
            c.moveTo(19.5, 20); c.lineTo(19.5, 14)
            c.quadraticCurveTo(19.5, 10, 15.5, 10)
            c.lineTo(4.5, 10)
            c.stroke()
        },
        "plus": function(c) {
            line(c, 12, 5, 12, 19); line(c, 5, 12, 19, 12)
        },
        "minus": function(c) {
            line(c, 5, 12, 19, 12)
        },
        "more": function(c) {
            // 省略号（"-" 图标的三点版本）：会话行溢出菜单入口。
            // 填实三个小圆点，与 Feather 的 more-horizontal 同形。
            c.beginPath()
            c.arc(6, 12, 1.5, 0, 2 * Math.PI, false)
            c.moveTo(13.5, 12)
            c.arc(12, 12, 1.5, 0, 2 * Math.PI, false)
            c.moveTo(19.5, 12)
            c.arc(18, 12, 1.5, 0, 2 * Math.PI, false)
            c.fill()
            c.stroke()
        },
        "branch": function(c) {
            // Git 分支（Feather "git-branch"）：右栏 Git 页签/分支头。
            circle(c, 6, 5.5, 2.5)
            circle(c, 6, 18.5, 2.5)
            circle(c, 18, 5.5, 2.5)
            line(c, 6, 8, 6, 16)
            c.beginPath()
            c.moveTo(18, 8)
            c.bezierCurveTo(18, 13.5, 13.5, 15.8, 8.5, 15.8)
            c.stroke()
        },
        "history": function(c) {
            // 时钟（Feather "clock"）：Git 面板的单文件提交历史入口。
            circle(c, 12, 12, 8.2)
            polyline(c, [12, 7.2, 12, 12, 15.8, 13.8])
        },
        "blame": function(c) {
            // 行号槽 + 三行代码：逐行归属（blame）入口。
            line(c, 5.2, 5.5, 5.2, 18.5)
            line(c, 8.6, 7.4, 18.6, 7.4)
            line(c, 8.6, 12, 16.2, 12)
            line(c, 8.6, 16.6, 18.6, 16.6)
            circle(c, 5.2, 12, 1.5)
        },
        // ── 设置中心分类导航（CategoryNav）──
        // 每个分类一个语义图标；只用路径基元，保证软件渲染后端也可见。
        "globe": function(c) {
            circle(c, 12, 12, 8.2)
            line(c, 3.8, 12, 20.2, 12)
            c.beginPath()
            c.moveTo(12, 3.8)
            c.bezierCurveTo(7.6, 6.4, 7.6, 17.6, 12, 20.2)
            c.bezierCurveTo(16.4, 17.6, 16.4, 6.4, 12, 3.8)
            c.stroke()
        },
        "book": function(c) {
            roundedRect(c, 3.8, 5.2, 7.4, 13.6, 1.4)
            roundedRect(c, 12.8, 5.2, 7.4, 13.6, 1.4)
            line(c, 12, 5.8, 12, 18.6)
        },
        "chart": function(c) {
            line(c, 4, 20, 20, 20)
            line(c, 7.5, 20, 7.5, 13.5)
            line(c, 12, 20, 12, 7.5)
            line(c, 16.5, 20, 16.5, 11)
        },
        "pulse": function(c) {
            polyline(c, [3.5, 12.5, 7.2, 12.5, 9.2, 7.5, 12.2, 17, 14.4, 10.5, 16.2, 12.5, 20.5, 12.5])
        },
        "list": function(c) {
            // 带项目符号的列表，与三横线 "menu" 区分。
            circle(c, 5, 7, 1.2); line(c, 9, 7, 20, 7)
            circle(c, 5, 12, 1.2); line(c, 9, 12, 20, 12)
            circle(c, 5, 17, 1.2); line(c, 9, 17, 20, 17)
        },
        "database": function(c) {
            // 圆柱体：顶面 + 两条侧边 + 两道弧线。
            c.beginPath()
            c.moveTo(5, 6)
            c.bezierCurveTo(5, 4.1, 8.1, 3.4, 12, 3.4)
            c.bezierCurveTo(15.9, 3.4, 19, 4.1, 19, 6)
            c.bezierCurveTo(19, 7.9, 15.9, 8.6, 12, 8.6)
            c.bezierCurveTo(8.1, 8.6, 5, 7.9, 5, 6)
            c.stroke()
            line(c, 5, 6, 5, 18)
            line(c, 19, 6, 19, 18)
            c.beginPath()
            c.moveTo(5, 18)
            c.bezierCurveTo(5, 19.9, 8.1, 20.6, 12, 20.6)
            c.bezierCurveTo(15.9, 20.6, 19, 19.9, 19, 18)
            c.stroke()
            c.beginPath()
            c.moveTo(5, 12)
            c.bezierCurveTo(5, 13.9, 8.1, 14.6, 12, 14.6)
            c.bezierCurveTo(15.9, 14.6, 19, 13.9, 19, 12)
            c.stroke()
        }
    })

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        ctx.scale(width / 24, height / 24)
        ctx.strokeStyle = String(iconColor)
        ctx.fillStyle = String(iconColor)
        ctx.lineWidth = 1.8
        ctx.lineCap = "round"
        ctx.lineJoin = "round"
        def(ctx)
    }

    Accessible.role: Accessible.Graphic
    Accessible.name: root.icon
}
