import QtQuick
import QtQuick.Window
import NexusAI
import NexusAI.ViewModels

// 技能页宿主：真实 SettingsPage 骨架 + 真实 AgentVM/SkillRegistry。
// C++ 侧（test_skills_qml.cpp）在加载前注入项目/用户技能目录，再通过
// refreshPage() 验证刷新按钮路径。
Window {
    id: win
    width: 900
    height: 760
    visible: true
    color: Theme.bgPrimary

    SkillsPage {
        id: skillsPage
        objectName: "skillsPage"
        anchors.fill: parent
    }

    function refreshPage() { skillsPage.refresh() }
    function skillCount() { return skillsPage.skills.length }
    function projectCount() { return skillsPage.projectSkills.length }
    function userCount() { return skillsPage.userSkills.length }
    function skillNames() {
        return skillsPage.skills.map(s => String(s.name))
    }
}