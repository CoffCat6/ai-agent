import QtQuick

// Vector icon painter: each icon is drawn from path primitives on a 24x24
// grid and scaled to `size`. Canvas (software raster) is used deliberately:
// the Shapes/GeometryRenderer path renders nothing when Qt Quick falls back
// to the software graphics backend, while Canvas works everywhere.
// Repaints are wired to every input so theme switches and hover recolors
// stay live.
//
// Usage: AppIcon { icon: "menu"; size: 20; iconColor: Theme.textPrimary }
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
        "menu": function(c) {
            line(c, 4, 6.5, 20, 6.5); line(c, 4, 12, 20, 12); line(c, 4, 17.5, 20, 17.5)
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
