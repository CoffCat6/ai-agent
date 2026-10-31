import QtQuick
import QtQuick.Window
import NexusAI

// docs/31 O4 / O11：导入预览弹窗的渲染回归宿主。
//
// 差异列表的数据由 C++ 侧用 setPreview() 直接塞进页面（不读真实备份
// 文件）——这里只验证"拿到 changes 之后，弹窗里是否真的按行渲染"。
// 纯 C++ 的 parseImport 用例看不到这类退化：数据对了，但 Repeater 的
// model 没绑上，用户看到的仍是一片空白。
Window {
    id: win
    width: 1000
    height: 760
    visible: true

    SettingsBackupPage {
        id: backupPage
        anchors.fill: parent
    }

    function setPreview(preview) {
        backupPage.importPath = "fake-backup.json"
        backupPage.importPreview = preview
    }
}
