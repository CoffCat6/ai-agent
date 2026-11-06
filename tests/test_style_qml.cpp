// NexusStyle 样式回归套件（docs/44 §8 AC-1 ~ AC-10 的可执行化版本）。
//
// 测试进程是独立可执行文件，访问不到 nexus_ai 的 qrc：根 CMakeLists 的
// nexus_add_style_resources() 把同一份 NexusStyle 资源编进本 target，
// main() 在 QGuiApplication 之后、任何 QML 引擎之前选中 NexusStyle。
//
// 断言口径：
//   * 不做像素/截图基线（offscreen 字体度量与抗锯齿不稳定），只比较
//     implicit 尺寸、QColor、边框宽度等可复现属性；
//   * 期望颜色一律从 ThemeProvider（C++ 单例，QML 的 Theme 同源）读取，
//     不写死 #RRGGBB，主题切换后用例仍成立；
//   * hover 无法通过公开属性直接驱动（QQuickControl::hovered 只读），
//     因此按任务约定退化为「派生 hover 颜色存在且与默认底色不同」，
//     该限制在最终报告中显式标注。
#include <QtTest>
#include <QColor>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QMetaObject>
#include <QStringList>

#include <cmath>
#include <memory>

#include "TestSettingsIsolation.h"
#include "viewmodels/ChangeBus.h"
#include "viewmodels/ThemeManager.h"
#include "viewmodels/ThemeProvider.h"

namespace {

// QML 声明式控件（含 Popup）不一定是 QQuickItem，统一按 QObject 查找。
QObject* findObject(QQuickWindow* window, const char* name)
{
    if (!window)
        return nullptr;
    const QString target = QString::fromLatin1(name);
    if (QObject* found = window->findChild<QObject*>(target))
        return found;
    if (QQuickItem* content = window->contentItem()) {
        if (QObject* found = content->findChild<QObject*>(target))
            return found;
    }
    return nullptr;
}

QObject* propertyObject(QObject* object, const char* name)
{
    return object ? object->property(name).value<QObject*>() : nullptr;
}

QColor propertyColor(QObject* object, const char* name)
{
    return object ? object->property(name).value<QColor>() : QColor();
}

qreal propertyReal(QObject* object, const char* name)
{
    return object ? object->property(name).toReal() : -1.0;
}

QObject* backgroundOf(QObject* control)
{
    return propertyObject(control, "background");
}

QObject* borderOf(QObject* rectangle)
{
    return propertyObject(rectangle, "border");
}

QObject* indicatorOf(QObject* control)
{
    return propertyObject(control, "indicator");
}

bool colorClose(const QColor& lhs, const QColor& rhs, int tolerance = 2)
{
    return lhs.isValid() && rhs.isValid()
        && std::abs(lhs.red() - rhs.red()) <= tolerance
        && std::abs(lhs.green() - rhs.green()) <= tolerance
        && std::abs(lhs.blue() - rhs.blue()) <= tolerance
        && std::abs(lhs.alpha() - rhs.alpha()) <= tolerance;
}

// WCAG 2.x 相对亮度。用于亮度方向断言（亮色下 press 必须比 hover 更深），
// 比逐个通道比较更贴近"视觉上更深/更浅"的语义。
double linearChannel(double value)
{
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const QColor& color)
{
    if (!color.isValid())
        return -1.0;
    return 0.2126 * linearChannel(color.redF())
         + 0.7152 * linearChannel(color.greenF())
         + 0.0722 * linearChannel(color.blueF());
}

// QML 的 `NumberAnimation on x` 以目标 Item 为 QObject parent 挂在对象树上，
// 用 QObject::children() 递归即可定位。不用 QML 的 resources 属性：它是
// non-bindable，readonly property 绑定会在动画对象创建前求值并永久为 false。
bool hasRunningAnimation(QObject* object)
{
    if (!object)
        return false;
    if (object->property("running").toBool())
        return true;
    const QObjectList children = object->children();
    for (QObject* child : children) {
        if (hasRunningAnimation(child))
            return true;
    }
    return false;
}

// 用例失败会提前 return，主题/动效是进程级单例；用 RAII 保证即使断言失败
// 也会恢复基线状态，避免污染后续用例（尤其是 darkThemeBaseline 与暗色口径）。
class ThemeModeGuard {
public:
    explicit ThemeModeGuard(const QString& original) : m_original(original) {}
    ~ThemeModeGuard()
    {
        NexusAI::ThemeManager::instance()->setThemeMode(m_original);
    }

private:
    QString m_original;
};

class ReduceMotionGuard {
public:
    ~ReduceMotionGuard()
    {
        if (m_active)
            NexusAI::ChangeBus::instance()->notify(
                QStringLiteral("ui.reduceMotion"), QVariant(false));
    }

    void enable()
    {
        // ChangeBus::notify 只广播内存态，不落盘；配合 TestSettingsIsolation
        // 不会污染用户偏好。ThemeManager 订阅后重算 Theme.motionFast。
        NexusAI::ChangeBus::instance()->notify(
            QStringLiteral("ui.reduceMotion"), QVariant(true));
        m_active = true;
    }

private:
    bool m_active = false;
};

// 每个用例独立加载一套夹具，避免上一用例的 enabled/down/focus 状态泄漏。
struct Harness {
    QObject* root() const { return m_root.get(); }
    QQuickWindow* window() const { return m_window; }
    QString error() const { return m_error; }

    bool load(const QString& fileName)
    {
        m_engine.addImportPath(QStringLiteral(NEXUS_BUILD_DIR));
        QQmlComponent component(
            &m_engine,
            QUrl::fromLocalFile(QStringLiteral(NEXUS_TEST_QML_DIR "/") + fileName));
        QObject* created = nullptr;
        if (!component.isReady()) {
            m_error = component.errorString();
            return false;
        }
        created = component.create();
        if (!created) {
            m_error = component.errorString();
            return false;
        }
        m_root.reset(created);
        m_window = qobject_cast<QQuickWindow*>(created);
        if (!m_window) {
            m_error = QStringLiteral("harness root is not a QQuickWindow");
            return false;
        }
        m_window->show();
        if (!QTest::qWaitForWindowExposed(m_window)) {
            m_error = QStringLiteral("harness window was not exposed");
            return false;
        }
        // 焦点态用例需要窗口处于激活状态（与 test_mention_interaction 同法）。
        m_window->requestActivate();
        QTest::qWait(150);
        return true;
    }

private:
    QQmlEngine m_engine;
    std::unique_ptr<QObject> m_root;
    QQuickWindow* m_window = nullptr;
    QString m_error;
};

QStringList g_capturedMessages;

void captureMessage(QtMsgType, const QMessageLogContext&, const QString& message)
{
    g_capturedMessages.append(message);
}

} // namespace

class TestStyleQml : public QObject {
    Q_OBJECT

private slots:
    void darkThemeBaseline();
    void controlSizes();
    void colorsAndRadii();
    void disabledFieldState();
    void pressedAndBrandStates();
    void focusRing();
    void comboBoxPopup();
    void menuPopupAndToolTip();
    void fallbackControls();
    void neutralHoverDerivation();
    // 任务 #13：样式实现修复（#11）的回归断言，追加在既有用例之后。
    void textFieldBackgroundOverride();
    void editableComboBoxFocus();
    void comboBoxPopupScrollBar();
    void scrollBarVisibilityBySize();
    void progressBarReduceMotion();
    void lightThemePressDirection();
    void sliderDisabledContrast();
};

// AC-4 前置：夹具必须运行在暗色主题（docs/44 §11 的实测基准值口径）。
void TestStyleQml::darkThemeBaseline()
{
    auto* theme = NexusAI::ThemeProvider::instance();
    QVERIFY(theme);
    QVERIFY2(theme->isDark(), "style suite must run under the dark theme baseline");
}

// AC-1/2/3/6/7/8/9：控件隐式尺寸与指示器几何。
void TestStyleQml::controlSizes()
{
    Harness harness;
    QVERIFY2(harness.load(QStringLiteral("StyleHarness.qml")),
             qPrintable(harness.error()));
    QQuickWindow* window = harness.window();
    QVERIFY(window);

    QObject* button = findObject(window, "button");
    QObject* toolButton = findObject(window, "toolButton");
    QObject* textField = findObject(window, "textField");
    QObject* textArea = findObject(window, "textArea");
    QObject* comboBox = findObject(window, "comboBox");
    QObject* switchItem = findObject(window, "switch");
    QObject* checkBox = findObject(window, "checkBox");
    QObject* radioButton = findObject(window, "radioButton");
    QObject* spinBox = findObject(window, "spinBox");
    QObject* slider = findObject(window, "slider");
    QObject* itemDelegate = findObject(window, "itemDelegate");
    QObject* progressBar = findObject(window, "progressBar");
    QObject* scrollBar = findObject(window, "scrollBar");
    QObject* menu = findObject(window, "menu");
    QObject* toolTip = findObject(window, "toolTip");
    QVERIFY(button);
    QVERIFY(toolButton);
    QVERIFY(textField);
    QVERIFY(textArea);
    QVERIFY(comboBox);
    QVERIFY(switchItem);
    QVERIFY(checkBox);
    QVERIFY(radioButton);
    QVERIFY(spinBox);
    QVERIFY(slider);
    QVERIFY(itemDelegate);
    QVERIFY(progressBar);
    QVERIFY(scrollBar);
    QVERIFY(menu);
    QVERIFY(toolTip);

    // AC-1：Button / ToolButton 高 32。
    QCOMPARE(propertyReal(button, "implicitHeight"), qreal(32));
    QCOMPARE(propertyReal(toolButton, "implicitHeight"), qreal(32));

    // AC-2：TextField 高 32；TextArea 最小高 64（≥60 为宽松口径，容忍字体度量）。
    QCOMPARE(propertyReal(textField, "implicitHeight"), qreal(32));
    QVERIFY2(propertyReal(textArea, "implicitHeight") >= 60.0,
             qPrintable(QStringLiteral("TextArea implicitHeight=%1")
                            .arg(propertyReal(textArea, "implicitHeight"))));

    // AC-3：ComboBox 收起态高 32。
    QCOMPARE(propertyReal(comboBox, "implicitHeight"), qreal(32));

    // AC-6：Switch 控件高 24，轨道 36×20。
    QCOMPARE(propertyReal(switchItem, "implicitHeight"), qreal(24));
    QObject* switchIndicator = indicatorOf(switchItem);
    QVERIFY(switchIndicator);
    QCOMPARE(propertyReal(switchIndicator, "implicitWidth"), qreal(36));
    QCOMPARE(propertyReal(switchIndicator, "implicitHeight"), qreal(20));
    // 滑块 16×16（focus 环与轨道之外的第三个 indicator 子项）。
    QQuickItem* switchIndicatorItem = qobject_cast<QQuickItem*>(switchIndicator);
    QVERIFY(switchIndicatorItem);
    bool thumbFound = false;
    for (QQuickItem* child : switchIndicatorItem->childItems()) {
        if (qFuzzyCompare(child->width(), qreal(16))
            && qFuzzyCompare(child->height(), qreal(16))) {
            thumbFound = true;
            break;
        }
    }
    QVERIFY2(thumbFound, "Switch thumb must be 16x16");

    // AC-7：CheckBox / RadioButton 至少 24 高、指示器 16×16。
    // 用 >=24 而非精确相等：用户字号大于 16px 时内容会自然撑高，精确值会
    // 变成对字体度量的脆断言（任务 #13 要求）。
    QVERIFY2(propertyReal(checkBox, "implicitHeight") >= 24.0,
             qPrintable(QStringLiteral("CheckBox implicitHeight=%1")
                            .arg(propertyReal(checkBox, "implicitHeight"))));
    QVERIFY2(propertyReal(radioButton, "implicitHeight") >= 24.0,
             qPrintable(QStringLiteral("RadioButton implicitHeight=%1")
                            .arg(propertyReal(radioButton, "implicitHeight"))));
    QObject* checkIndicator = indicatorOf(checkBox);
    QObject* radioIndicator = indicatorOf(radioButton);
    QVERIFY(checkIndicator);
    QVERIFY(radioIndicator);
    QCOMPARE(propertyReal(checkIndicator, "implicitWidth"), qreal(16));
    QCOMPARE(propertyReal(checkIndicator, "implicitHeight"), qreal(16));
    QCOMPARE(propertyReal(radioIndicator, "implicitWidth"), qreal(16));
    QCOMPARE(propertyReal(radioIndicator, "implicitHeight"), qreal(16));

    // AC-8：SpinBox 高 32、按钮列宽 20；Slider 高 24、滑块 14。
    QCOMPARE(propertyReal(spinBox, "implicitHeight"), qreal(32));
    QCOMPARE(propertyReal(slider, "implicitHeight"), qreal(24));
    // up/down 是 QQuickIndicatorButton 包装；尺寸在它托管的 indicator 上。
    QObject* spinUp = propertyObject(propertyObject(spinBox, "up"), "indicator");
    QObject* spinDown = propertyObject(propertyObject(spinBox, "down"), "indicator");
    QVERIFY(spinUp);
    QVERIFY(spinDown);
    QCOMPARE(propertyReal(spinUp, "implicitWidth"), qreal(20));
    QCOMPARE(propertyReal(spinUp, "implicitHeight"), qreal(16));
    QCOMPARE(propertyReal(spinDown, "implicitWidth"), qreal(20));
    QCOMPARE(propertyReal(spinDown, "implicitHeight"), qreal(16));
    QObject* sliderHandle = propertyObject(slider, "handle");
    QVERIFY(sliderHandle);
    QCOMPARE(propertyReal(sliderHandle, "implicitWidth"), qreal(14));
    QCOMPARE(propertyReal(sliderHandle, "implicitHeight"), qreal(14));

    // AC-9：ItemDelegate 高 30；ProgressBar 高 4；ScrollBar 12×12。
    QCOMPARE(propertyReal(itemDelegate, "implicitHeight"), qreal(30));
    QCOMPARE(propertyReal(progressBar, "implicitHeight"), qreal(4));
    QCOMPARE(propertyReal(scrollBar, "implicitWidth"), qreal(12));
    QCOMPARE(propertyReal(scrollBar, "implicitHeight"), qreal(12));
    QObject* scrollBarThumb = propertyObject(scrollBar, "contentItem");
    QVERIFY(scrollBarThumb);
    QCOMPARE(propertyReal(scrollBarThumb, "implicitWidth"), qreal(8));
    QCOMPARE(propertyReal(scrollBarThumb, "implicitHeight"), qreal(8));

    // AC-9：Menu 最小宽 160；ToolTip 单行不低于 24。
    QVERIFY2(propertyReal(menu, "implicitWidth") >= 160.0,
             qPrintable(QStringLiteral("Menu implicitWidth=%1")
                            .arg(propertyReal(menu, "implicitWidth"))));
    QVERIFY2(propertyReal(toolTip, "implicitHeight") >= 24.0,
             qPrintable(QStringLiteral("ToolTip implicitHeight=%1")
                            .arg(propertyReal(toolTip, "implicitHeight"))));
}

// AC-1/2/3/5/9：圆角与背景色走 Theme token，而不是各控件硬编码。
void TestStyleQml::colorsAndRadii()
{
    auto* theme = NexusAI::ThemeProvider::instance();
    QVERIFY(theme);

    Harness harness;
    QVERIFY2(harness.load(QStringLiteral("StyleHarness.qml")),
             qPrintable(harness.error()));
    QQuickWindow* window = harness.window();
    QVERIFY(window);

    QObject* button = findObject(window, "button");
    QObject* textField = findObject(window, "textField");
    QObject* textArea = findObject(window, "textArea");
    QObject* comboBox = findObject(window, "comboBox");
    QObject* slider = findObject(window, "slider");
    QObject* progressBar = findObject(window, "progressBar");
    QObject* menu = findObject(window, "menu");
    QVERIFY(button);
    QVERIFY(textField);
    QVERIFY(textArea);
    QVERIFY(comboBox);
    QVERIFY(slider);
    QVERIFY(progressBar);
    QVERIFY(menu);

    QObject* buttonBackground = backgroundOf(button);
    QVERIFY(buttonBackground);
    QCOMPARE(propertyReal(buttonBackground, "radius"), qreal(theme->radiusField()));
    QCOMPARE(propertyColor(buttonBackground, "color"), theme->surfaceHover());

    // AC-2：TextField / TextArea 默认底 bgPrimary，圆角 radiusField。
    for (QObject* field : {textField, textArea}) {
        QObject* background = backgroundOf(field);
        QVERIFY(background);
        QCOMPARE(propertyColor(background, "color"), theme->bgPrimary());
        QCOMPARE(propertyReal(background, "radius"), qreal(theme->radiusField()));
    }

    // AC-3：ComboBox 收起态与输入框同族。
    QObject* comboBackground = backgroundOf(comboBox);
    QVERIFY(comboBackground);
    QCOMPARE(propertyColor(comboBackground, "color"), theme->bgPrimary());
    QCOMPARE(propertyReal(comboBackground, "radius"), qreal(theme->radiusField()));

    // AC-3/5：Menu 弹层 = 卡片底 + 1px 卡片边框。
    QObject* menuBackground = backgroundOf(menu);
    QVERIFY(menuBackground);
    QCOMPARE(propertyColor(menuBackground, "color"), theme->cardBackground());
    QObject* menuBorder = borderOf(menuBackground);
    QVERIFY(menuBorder);
    QCOMPARE(propertyReal(menuBorder, "width"), qreal(1));
    QCOMPARE(propertyColor(menuBorder, "color"), theme->cardBorder());

    // AC-8/9：Slider / ProgressBar 轨道同为 borderSubtle。
    QObject* sliderBackground = backgroundOf(slider);
    QObject* progressBackground = backgroundOf(progressBar);
    QVERIFY(sliderBackground);
    QVERIFY(progressBackground);
    QCOMPARE(propertyColor(sliderBackground, "color"), theme->borderSubtle());
    QCOMPARE(propertyColor(progressBackground, "color"), theme->borderSubtle());
}

// AC-4（禁用态 S4）：TextField 三 token 直接驱动，不依赖鼠标模拟。
void TestStyleQml::disabledFieldState()
{
    auto* theme = NexusAI::ThemeProvider::instance();
    Harness harness;
    QVERIFY2(harness.load(QStringLiteral("StyleHarness.qml")),
             qPrintable(harness.error()));
    QQuickWindow* window = harness.window();
    QVERIFY(window);

    QObject* textField = findObject(window, "textField");
    QObject* comboBox = findObject(window, "comboBox");
    QVERIFY(textField);
    QVERIFY(comboBox);

    QObject* fieldBackground = backgroundOf(textField);
    QObject* fieldBorder = borderOf(fieldBackground);
    QVERIFY(fieldBackground);
    QVERIFY(fieldBorder);

    textField->setProperty("enabled", false);
    QCoreApplication::processEvents();
    QCOMPARE(propertyColor(fieldBackground, "color"), theme->controlBackgroundDisabled());
    QCOMPARE(propertyColor(fieldBorder, "color"), theme->controlBorderDisabled());
    QCOMPARE(propertyColor(textField, "color"), theme->controlTextDisabled());

    // ComboBox 收起态与 TextField 同口径（docs/44 §2.2 「字段/选项类」）。
    QObject* comboBackground = backgroundOf(comboBox);
    QObject* comboBorder = borderOf(comboBackground);
    QVERIFY(comboBackground);
    QVERIFY(comboBorder);
    comboBox->setProperty("enabled", false);
    QCoreApplication::processEvents();
    QCOMPARE(propertyColor(comboBackground, "color"), theme->controlBackgroundDisabled());
    QCOMPARE(propertyColor(comboBorder, "color"), theme->controlBorderDisabled());

    // 反向对照：恢复 enabled 后必须回到默认态，防止「禁用分支粘住」。
    textField->setProperty("enabled", true);
    comboBox->setProperty("enabled", true);
    QCoreApplication::processEvents();
    QCOMPARE(propertyColor(fieldBackground, "color"), theme->bgPrimary());
    QCOMPARE(propertyColor(fieldBorder, "color"), theme->borderSubtle());
    QCOMPARE(propertyColor(comboBackground, "color"), theme->bgPrimary());
}

// AC-4（按压 S2 / 品牌填充 S0'~S2'）：down 是 AbstractButton 的可写属性，
// 直接赋值即可避免鼠标事件模拟。
void TestStyleQml::pressedAndBrandStates()
{
    auto* theme = NexusAI::ThemeProvider::instance();
    Harness harness;
    QVERIFY2(harness.load(QStringLiteral("StyleHarness.qml")),
             qPrintable(harness.error()));
    QQuickWindow* window = harness.window();
    QVERIFY(window);

    QObject* button = findObject(window, "button");
    QObject* highlighted = findObject(window, "highlightedButton");
    QObject* textField = findObject(window, "textField");
    QVERIFY(button);
    QVERIFY(highlighted);
    QVERIFY(textField);

    QObject* buttonBackground = backgroundOf(button);
    QObject* highlightedBackground = backgroundOf(highlighted);
    QObject* fieldBackground = backgroundOf(textField);
    QObject* fieldBorder = borderOf(fieldBackground);
    QVERIFY(buttonBackground);
    QVERIFY(highlightedBackground);
    QVERIFY(fieldBackground);
    QVERIFY(fieldBorder);

    // 中性按钮 press → controlBackgroundPressed；释放回 surfaceHover。
    button->setProperty("down", true);
    QCoreApplication::processEvents();
    QCOMPARE(propertyColor(buttonBackground, "color"), theme->controlBackgroundPressed());
    button->setProperty("down", false);
    QCoreApplication::processEvents();
    QCOMPARE(propertyColor(buttonBackground, "color"), theme->surfaceHover());

    // 品牌填充按钮：默认 brandPrimary → press controlBrandPressed。
    QCOMPARE(propertyColor(highlightedBackground, "color"), theme->brandPrimary());
    highlighted->setProperty("down", true);
    QCoreApplication::processEvents();
    QCOMPARE(propertyColor(highlightedBackground, "color"), theme->controlBrandPressed());
    highlighted->setProperty("down", false);
    QCoreApplication::processEvents();
    QCOMPARE(propertyColor(highlightedBackground, "color"), theme->brandPrimary());

    // 字段类 press：样式用自定义 pressActive 跟随 pressed 信号，属性可直接驱动。
    QVERIFY2(textField->property("pressActive").isValid(),
             "TextField style must expose pressActive for the press state");
    textField->setProperty("pressActive", true);
    QCoreApplication::processEvents();
    QCOMPARE(propertyColor(fieldBackground, "color"), theme->controlBackgroundPressed());
    QCOMPARE(propertyColor(fieldBorder, "color"), theme->cardBorder());
    textField->setProperty("pressActive", false);
    QCoreApplication::processEvents();
    QCOMPARE(propertyColor(fieldBackground, "color"), theme->bgPrimary());
    QCOMPARE(propertyColor(fieldBorder, "color"), theme->borderSubtle());
}

// AC-3/4（焦点 S3）：focus 走真实焦点链，不写只读的 visualFocus。
void TestStyleQml::focusRing()
{
    auto* theme = NexusAI::ThemeProvider::instance();
    Harness harness;
    QVERIFY2(harness.load(QStringLiteral("StyleHarness.qml")),
             qPrintable(harness.error()));
    QQuickWindow* window = harness.window();
    QVERIFY(window);

    QQuickItem* textField = qobject_cast<QQuickItem*>(findObject(window, "textField"));
    QQuickItem* button = qobject_cast<QQuickItem*>(findObject(window, "button"));
    QQuickItem* highlighted = qobject_cast<QQuickItem*>(findObject(window, "highlightedButton"));
    QVERIFY(textField);
    QVERIFY(button);
    QVERIFY(highlighted);

    QObject* fieldBackground = backgroundOf(textField);
    QObject* fieldBorder = borderOf(fieldBackground);
    QVERIFY(fieldBackground);
    QVERIFY(fieldBorder);

    textField->forceActiveFocus();
    QTRY_VERIFY_WITH_TIMEOUT(textField->property("activeFocus").toBool(), 1000);
    QCOMPARE(propertyReal(fieldBorder, "width"), qreal(2));
    QCOMPARE(propertyColor(fieldBorder, "color"), theme->brandPrimary());

    // 中性 Button：visualFocus → 2px brandPrimary。
    // visualFocus 只在键盘导航来源下置位（鼠标/程序焦点不算键盘可见焦点），
    // 因此显式用 TabFocusReason，而不是无参 forceActiveFocus。
    button->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY_WITH_TIMEOUT(button->property("visualFocus").toBool(), 1000);
    QObject* buttonBorder = borderOf(backgroundOf(button));
    QVERIFY(buttonBorder);
    QCOMPARE(propertyReal(buttonBorder, "width"), qreal(2));
    QCOMPARE(propertyColor(buttonBorder, "color"), theme->brandPrimary());

    // 品牌填充 Button：焦点环必须换成 bgPrimary，否则与填充同色不可见（R13）。
    highlighted->forceActiveFocus(Qt::TabFocusReason);
    QTRY_VERIFY_WITH_TIMEOUT(highlighted->property("visualFocus").toBool(), 1000);
    QObject* highlightedBorder = borderOf(backgroundOf(highlighted));
    QVERIFY(highlightedBorder);
    QCOMPARE(propertyReal(highlightedBorder, "width"), qreal(2));
    QCOMPARE(propertyColor(highlightedBorder, "color"), theme->bgPrimary());
}

// AC-5：ComboBox 弹层底色/边框/选项行高。
void TestStyleQml::comboBoxPopup()
{
    auto* theme = NexusAI::ThemeProvider::instance();
    Harness harness;
    QVERIFY2(harness.load(QStringLiteral("StyleHarness.qml")),
             qPrintable(harness.error()));
    QQuickWindow* window = harness.window();
    QVERIFY(window);

    QObject* comboBox = findObject(window, "comboBox");
    QVERIFY(comboBox);
    QObject* popup = propertyObject(comboBox, "popup");
    QVERIFY2(popup, "ComboBox must expose the inline popup");

    QVERIFY(QMetaObject::invokeMethod(popup, "open"));
    QTRY_VERIFY_WITH_TIMEOUT(popup->property("opened").toBool()
                                 || popup->property("visible").toBool(),
                             2000);

    QObject* popupBackground = backgroundOf(popup);
    QVERIFY(popupBackground);
    QCOMPARE(propertyColor(popupBackground, "color"), theme->cardBackground());
    QObject* popupBorder = borderOf(popupBackground);
    QVERIFY(popupBorder);
    QCOMPARE(propertyReal(popupBorder, "width"), qreal(1));
    QCOMPARE(propertyColor(popupBorder, "color"), theme->cardBorder());

    QVERIFY2(propertyReal(popup, "width") >= 160.0,
             qPrintable(QStringLiteral("ComboBox popup width=%1")
                            .arg(propertyReal(popup, "width"))));

    // 选项行由同目录 ItemDelegate 绘制（高 30）。delegate 在 offscreen 下可能
    // 尚未被 instance model 孵化为可枚举的子项，因此用弹层 ListView 的
    // contentHeight（= count × 行高）断言实际生效的行高，而不是依赖子项时序。
    QQuickItem* listView = qobject_cast<QQuickItem*>(propertyObject(popup, "contentItem"));
    QVERIFY(listView);
    QCOMPARE(listView->property("count").toInt(), 3);
    QCOMPARE(listView->property("contentHeight").toReal(), qreal(3 * 30));

    QVERIFY(QMetaObject::invokeMethod(popup, "close"));
}

// AC-9：Menu 弹层与 ToolTip 的几何/配色。
void TestStyleQml::menuPopupAndToolTip()
{
    auto* theme = NexusAI::ThemeProvider::instance();
    Harness harness;
    QVERIFY2(harness.load(QStringLiteral("StyleHarness.qml")),
             qPrintable(harness.error()));
    QQuickWindow* window = harness.window();
    QVERIFY(window);

    QObject* menu = findObject(window, "menu");
    QObject* toolTip = findObject(window, "toolTip");
    QVERIFY(menu);
    QVERIFY(toolTip);

    QVERIFY(QMetaObject::invokeMethod(menu, "open"));
    QTRY_VERIFY_WITH_TIMEOUT(menu->property("opened").toBool()
                                 || menu->property("visible").toBool(),
                             2000);

    QObject* menuBackground = backgroundOf(menu);
    QVERIFY(menuBackground);
    QCOMPARE(propertyColor(menuBackground, "color"), theme->cardBackground());
    QObject* menuBorder = borderOf(menuBackground);
    QVERIFY(menuBorder);
    QCOMPARE(propertyReal(menuBorder, "width"), qreal(1));
    QCOMPARE(propertyColor(menuBorder, "color"), theme->cardBorder());

    // AC-9：MenuItem 行高 30、MenuSeparator 1px 线 + 上下 4px = 9。
    QObject* menuItem = findObject(window, "menuItem");
    QObject* menuSeparator = findObject(window, "menuSeparator");
    QVERIFY2(menuItem, "MenuItem inside the menu was not instantiated");
    QVERIFY2(menuSeparator, "MenuSeparator inside the menu was not instantiated");
    QCOMPARE(propertyReal(menuItem, "implicitHeight"), qreal(30));
    QCOMPARE(propertyReal(menuSeparator, "implicitHeight"), qreal(9));
    QObject* separatorLine = propertyObject(menuSeparator, "contentItem");
    QVERIFY(separatorLine);
    QCOMPARE(propertyColor(separatorLine, "color"), theme->borderSubtle());

    QObject* toolTipBackground = backgroundOf(toolTip);
    QVERIFY(toolTipBackground);
    QCOMPARE(propertyColor(toolTipBackground, "color"), theme->bgSecondary());
    QObject* toolTipBorder = borderOf(toolTipBackground);
    QVERIFY(toolTipBorder);
    QCOMPARE(propertyColor(toolTipBorder, "color"), theme->borderSubtle());

    QVERIFY(QMetaObject::invokeMethod(menu, "close"));
}

// AC-10：未在 qmldir 登记的控件回退 Basic，且不产生 "not installed" 警告。
void TestStyleQml::fallbackControls()
{
    g_capturedMessages.clear();
    QtMessageHandler previous = qInstallMessageHandler(captureMessage);

    Harness harness;
    const bool loaded = harness.load(QStringLiteral("StyleFallbackHarness.qml"));
    QTest::qWait(200);
    qInstallMessageHandler(previous);

    QVERIFY2(loaded, qPrintable(harness.error()));
    QQuickWindow* window = harness.window();
    QVERIFY(window);

    QObject* tabBar = findObject(window, "fallbackTabBar");
    QObject* dial = findObject(window, "fallbackDial");
    QVERIFY2(tabBar, "TabBar was not instantiated (style fallback failed)");
    QVERIFY2(dial, "Dial was not instantiated (style fallback failed)");
    QVERIFY2(propertyReal(tabBar, "implicitHeight") > 0.0,
             "TabBar implicit size collapsed");
    QVERIFY2(propertyReal(dial, "implicitWidth") > 0.0,
             "Dial implicit size collapsed");

    for (const QString& message : std::as_const(g_capturedMessages)) {
        QVERIFY2(!message.contains(QStringLiteral("not installed"), Qt::CaseInsensitive),
                 qPrintable(QStringLiteral("unexpected fallback warning: %1").arg(message)));
    }
}

// AC-4（hover S1）的降级验证：hovered 只读且 offscreen 无可靠指针，
// 因此锁定「派生 hover 颜色确实存在且与默认底不同」＋禁用分支口径。
void TestStyleQml::neutralHoverDerivation()
{
    auto* theme = NexusAI::ThemeProvider::instance();
    Harness harness;
    QVERIFY2(harness.load(QStringLiteral("StyleHarness.qml")),
             qPrintable(harness.error()));
    QQuickWindow* window = harness.window();
    QVERIFY(window);

    QObject* button = findObject(window, "button");
    QObject* textField = findObject(window, "textField");
    QObject* comboBox = findObject(window, "comboBox");
    QVERIFY(button);
    QVERIFY(textField);
    QVERIFY(comboBox);

    // 平台事实（实测，保留输出作为证据）：QQuickControl::hoverEnabled 的默认值
    // 跟随平台的 hover 效果——windows 平台 Button=true，offscreen 平台
    // Button=false。offscreen 下 hover 分支不可达，因此本用例只验证「派生色
    // 存在且与默认底不同」＋禁用口径；hover 触发在最终报告中标注「未直接验证」。
    qInfo() << "hoverEnabled: button" << button->property("hoverEnabled")
            << "textField" << textField->property("hoverEnabled")
            << "comboBox" << comboBox->property("hoverEnabled");
    // 字段类在样式里显式打开 hoverEnabled，与平台无关，因此可直接断言。
    QCOMPARE(textField->property("hoverEnabled").toBool(), true);
    QCOMPARE(comboBox->property("hoverEnabled").toBool(), true);

    const QColor neutralHover = propertyColor(button, "neutralHoverColor");
    QVERIFY2(neutralHover.isValid(), "Button must expose neutralHoverColor");
    const QColor expected = theme->isDark()
        ? theme->surfaceHover().lighter(120)
        : theme->surfaceHover().darker(107);
    QVERIFY2(colorClose(neutralHover, expected),
             qPrintable(QStringLiteral("neutralHoverColor=%1 expected=%2")
                            .arg(neutralHover.name(), expected.name())));
    QVERIFY2(neutralHover != theme->surfaceHover(),
             "hover color must differ from the default button background");

    // 中性按钮禁用态：样式约定整体 0.5 透明 + 保持 surfaceHover（docs/44 §2.2）。
    button->setProperty("enabled", false);
    QCoreApplication::processEvents();
    QCOMPARE(propertyColor(backgroundOf(button), "color"), theme->surfaceHover());
    QCOMPARE(propertyReal(button, "opacity"), qreal(0.5));
}

// 任务 #13：局部 background 覆盖回归。
//
// 业务有 29 处 `background: Rectangle` 覆盖（且不设 implicitHeight）。样式
// 若把 TextField 上下 padding 归零，implicitHeight 会退化为 contentHeight，
// 输入框被压塌到 17-20px。本用例锁定 padding=6 与 [28, 32) 的高度区间。
void TestStyleQml::textFieldBackgroundOverride()
{
    Harness harness;
    QVERIFY2(harness.load(QStringLiteral("StyleHarness.qml")),
             qPrintable(harness.error()));
    QQuickWindow* window = harness.window();
    QVERIFY(window);

    QObject* textField = findObject(window, "textField");
    QObject* customField = findObject(window, "textFieldCustomBackground");
    QObject* textArea = findObject(window, "textArea");
    QObject* customArea = findObject(window, "textAreaCustomBackground");
    QVERIFY(textField);
    QVERIFY(customField);
    QVERIFY(textArea);
    QVERIFY(customArea);

    // 默认背景仍由 background.implicitHeight 兜底到 32。
    QCOMPARE(propertyReal(textField, "implicitHeight"), qreal(32));

    // 局部背景无 implicitHeight → implicitHeight 退化为 contentHeight + 上下 padding。
    const qreal customFieldHeight = propertyReal(customField, "implicitHeight");
    const qreal fieldContentHeight = customField->property("contentHeight").toReal();
    QVERIFY2(fieldContentHeight > 0.0,
             qPrintable(QStringLiteral("TextField contentHeight=%1").arg(fieldContentHeight)));
    // 根因锁定：padding 6+6 必须参与 implicitHeight（contentHeight + 12）。
    QVERIFY2(customFieldHeight >= fieldContentHeight + 12.0 - 0.01,
             qPrintable(QStringLiteral("implicitHeight=%1 must cover contentHeight+12=%2")
                            .arg(customFieldHeight)
                            .arg(fieldContentHeight + 12.0)));
    // 不压塌：padding 丢失时只剩 contentHeight（~14px）。offscreen 字体度量下
    // 实测 contentHeight=14 → 26px，所以下限取 24；区间仍能捕获 17-20px 回归。
    QVERIFY2(customFieldHeight >= 24.0 && customFieldHeight < 32.0,
             qPrintable(QStringLiteral("custom-background TextField implicitHeight=%1 "
                                       "(expected [24, 32))")
                            .arg(customFieldHeight)));

    // 根因锁定：上下 padding 必须是 6，而不是被改成 0/8。
    QCOMPARE(customField->property("topPadding").toReal(), qreal(6));
    QCOMPARE(customField->property("bottomPadding").toReal(), qreal(6));

    // TextArea 默认背景仍有 64 最小高（≥60 宽松口径，容忍字体度量）。
    QVERIFY2(propertyReal(textArea, "implicitHeight") >= 60.0,
             qPrintable(QStringLiteral("TextArea implicitHeight=%1")
                            .arg(propertyReal(textArea, "implicitHeight"))));
    QCOMPARE(customArea->property("topPadding").toReal(), qreal(6));
    QCOMPARE(customArea->property("bottomPadding").toReal(), qreal(6));

    // 局部背景的 TextArea 同样不能压塌：padding 6+6 必须参与高度计算。
    const qreal customAreaHeight = propertyReal(customArea, "implicitHeight");
    const qreal areaContentHeight = customArea->property("contentHeight").toReal();
    QVERIFY2(customAreaHeight >= areaContentHeight + 12.0 - 0.01,
             qPrintable(QStringLiteral("TextArea implicitHeight=%1 must cover "
                                       "contentHeight+12=%2")
                            .arg(customAreaHeight)
                            .arg(areaContentHeight + 12.0)));
    QVERIFY2(customAreaHeight >= 24.0 && customAreaHeight < 64.0,
             qPrintable(QStringLiteral("custom-background TextArea implicitHeight=%1 "
                                       "(expected [24, 64))")
                            .arg(customAreaHeight)));
}

// 任务 #13：可编辑 ComboBox 的焦点环。
//
// editable 时 Tab 焦点落在 contentItem（内嵌 T.TextField），
// control.activeFocus/visualFocus 都为 false；样式的 focused 聚合属性必须兜底。
void TestStyleQml::editableComboBoxFocus()
{
    auto* theme = NexusAI::ThemeProvider::instance();
    QVERIFY(theme);

    Harness harness;
    QVERIFY2(harness.load(QStringLiteral("StyleHarness.qml")),
             qPrintable(harness.error()));
    QQuickWindow* window = harness.window();
    QVERIFY(window);

    QObject* comboBox = findObject(window, "editableComboBox");
    QVERIFY(comboBox);
    QCOMPARE(comboBox->property("editable").toBool(), true);

    QObject* background = backgroundOf(comboBox);
    QObject* border = borderOf(background);
    QVERIFY(background);
    QVERIFY(border);

    QQuickItem* button = qobject_cast<QQuickItem*>(findObject(window, "button"));
    QQuickItem* contentItem =
        qobject_cast<QQuickItem*>(propertyObject(comboBox, "contentItem"));
    QVERIFY(button);
    QVERIFY(contentItem);

    // 先让焦点离场，确认 focused 是跟随焦点变化的绑定，而不是恒 true。
    button->forceActiveFocus();
    QTRY_VERIFY_WITH_TIMEOUT(!comboBox->property("focused").toBool(), 1500);
    QCOMPARE(propertyReal(border, "width"), qreal(1));

    // 焦点交给内嵌 contentItem：ComboBox 自身 activeFocus 为 false，
    // focused 仍必须为 true，边框变成 2px brandPrimary。
    contentItem->forceActiveFocus();
    QTRY_VERIFY_WITH_TIMEOUT(contentItem->property("activeFocus").toBool(), 1500);
    QTRY_VERIFY_WITH_TIMEOUT(comboBox->property("focused").toBool(), 1500);

    QCOMPARE(propertyReal(border, "width"), qreal(2));
    QCOMPARE(propertyColor(border, "color"), theme->brandPrimary());
}

// 任务 #13：ComboBox 弹层滚动条挂载 + 长模型高度上限。
void TestStyleQml::comboBoxPopupScrollBar()
{
    Harness harness;
    QVERIFY2(harness.load(QStringLiteral("StyleHarness.qml")),
             qPrintable(harness.error()));
    QQuickWindow* window = harness.window();
    QVERIFY(window);

    QObject* comboBox = findObject(window, "comboBox");
    QObject* longComboBox = findObject(window, "longComboBox");
    QVERIFY(comboBox);
    QVERIFY(longComboBox);

    QObject* popup = propertyObject(comboBox, "popup");
    QVERIFY2(popup, "ComboBox must expose the inline popup");
    QVERIFY(QMetaObject::invokeMethod(popup, "open"));
    QTRY_VERIFY_WITH_TIMEOUT(popup->property("opened").toBool()
                                 || popup->property("visible").toBool(),
                             2000);
    // 附加属性 ScrollBar.vertical 已挂到 popup.contentItem（ListView）上。
    QTRY_VERIFY_WITH_TIMEOUT(harness.root()->property("comboHasScrollBar").toBool(), 1500);

    QObject* longPopup = propertyObject(longComboBox, "popup");
    QVERIFY2(longPopup, "long-model ComboBox must expose the inline popup");
    QVERIFY(QMetaObject::invokeMethod(longPopup, "open"));
    QTRY_VERIFY_WITH_TIMEOUT(longPopup->property("opened").toBool()
                                 || longPopup->property("visible").toBool(),
                             2000);

    // 15 项 × 30px = 450 > 320 上限；弹层高度必须被截断且贴近上限。
    const qreal popupHeight = propertyReal(longPopup, "height");
    QVERIFY2(popupHeight <= 320.0,
             qPrintable(QStringLiteral("long ComboBox popup height=%1 (expected <= 320)")
                            .arg(popupHeight)));
    QVERIFY2(popupHeight >= 300.0,
             qPrintable(QStringLiteral("long ComboBox popup height=%1 (expected >= 300)")
                            .arg(popupHeight)));

    // 内容溢出时 AsNeeded 滚动条应可见（size < 1.0）。
    QTRY_VERIFY_WITH_TIMEOUT(harness.root()->property("longComboScrollBarVisible").toBool(),
                             1500);

    QVERIFY(QMetaObject::invokeMethod(longPopup, "close"));
    QVERIFY(QMetaObject::invokeMethod(popup, "close"));
}

// 任务 #13：ScrollBar 可见性矩阵。
//
// 修复后口径：AlwaysOn 恒显；AsNeeded 仅 size < 1.0（确实溢出）时可见；
// size == 1.0 不再渲染一根撑满全高的假滚动条。
void TestStyleQml::scrollBarVisibilityBySize()
{
    Harness harness;
    QVERIFY2(harness.load(QStringLiteral("StyleHarness.qml")),
             qPrintable(harness.error()));
    QQuickWindow* window = harness.window();
    QVERIFY(window);

    QObject* fullSize = findObject(window, "scrollBarFullSize");
    QObject* halfSize = findObject(window, "scrollBarHalfSize");
    QObject* alwaysOnFullSize = findObject(window, "scrollBarAlwaysOnFullSize");
    QVERIFY(fullSize);
    QVERIFY(halfSize);
    QVERIFY(alwaysOnFullSize);

    QCOMPARE(propertyReal(fullSize, "size"), qreal(1.0));
    QCOMPARE(propertyReal(halfSize, "size"), qreal(0.5));
    QCOMPARE(propertyReal(alwaysOnFullSize, "size"), qreal(1.0));

    QCOMPARE(fullSize->property("visible").toBool(), false);
    QCOMPARE(halfSize->property("visible").toBool(), true);
    QCOMPARE(alwaysOnFullSize->property("visible").toBool(), true);
}

// 任务 #13：reduceMotion 下 indeterminate ProgressBar 停止扫动。
//
// 不能只断言 Theme.motionFast == 0：真正要验证的是 Animation.running
// 绑定在 motionFast 上也随之为 false，所以通过 harness 递归检查 resources
// 里的 NumberAnimation 实例。
void TestStyleQml::progressBarReduceMotion()
{
    auto* theme = NexusAI::ThemeProvider::instance();
    auto* bus = NexusAI::ChangeBus::instance();
    QVERIFY(theme);
    QVERIFY(bus);

    ReduceMotionGuard guard;
    QCOMPARE(theme->motionFast(), 100);

    Harness harness;
    QVERIFY2(harness.load(QStringLiteral("StyleHarness.qml")),
             qPrintable(harness.error()));
    QQuickWindow* window = harness.window();
    QVERIFY(window);

    QObject* progressBar = findObject(window, "indeterminateProgressBar");
    QVERIFY(progressBar);
    QCOMPARE(progressBar->property("indeterminate").toBool(), true);
    QObject* contentItem = propertyObject(progressBar, "contentItem");
    QVERIFY2(contentItem, "ProgressBar must expose its contentItem");
    QTRY_VERIFY_WITH_TIMEOUT(hasRunningAnimation(contentItem), 3000);

    guard.enable();
    QTRY_COMPARE_WITH_TIMEOUT(theme->motionFast(), 0, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(!hasRunningAnimation(contentItem), 3000);

    // 恢复 reduceMotion=false 后动画必须重新跑起来，防止 running 被写死为 false。
    bus->notify(QStringLiteral("ui.reduceMotion"), QVariant(false));
    QTRY_COMPARE_WITH_TIMEOUT(theme->motionFast(), 100, 2000);
    QTRY_VERIFY_WITH_TIMEOUT(hasRunningAnimation(contentItem), 3000);
}

// 任务 #13：亮色主题下 press 必须比 hover 更深（亮度方向不能反）。
void TestStyleQml::lightThemePressDirection()
{
    auto* theme = NexusAI::ThemeProvider::instance();
    auto* manager = NexusAI::ThemeManager::instance();
    QVERIFY(theme);
    QVERIFY(manager);

    ThemeModeGuard guard(manager->themeMode());
    manager->setThemeMode(QStringLiteral("light"));
    QTRY_VERIFY_WITH_TIMEOUT(!theme->isDark(), 2000);
    QCoreApplication::processEvents();

    Harness harness;
    QVERIFY2(harness.load(QStringLiteral("StyleHarness.qml")),
             qPrintable(harness.error()));
    QQuickWindow* window = harness.window();
    QVERIFY(window);

    QObject* button = findObject(window, "button");
    QVERIFY(button);
    QObject* background = backgroundOf(button);
    QVERIFY(background);

    const QColor hoverColor = button->property("neutralHoverColor").value<QColor>();
    const QColor pressedColor = button->property("neutralPressedColor").value<QColor>();
    QVERIFY2(hoverColor.isValid(), "Button must expose neutralHoverColor");
    QVERIFY2(pressedColor.isValid(), "Button must expose neutralPressedColor");

    // 亮色口径：hover 压暗 1.07、press 再压到 1.14；两者都从 surfaceHover 派生。
    QVERIFY2(colorClose(hoverColor, theme->surfaceHover().darker(107)),
             qPrintable(QStringLiteral("neutralHoverColor=%1 expected=%2")
                            .arg(hoverColor.name(),
                                 theme->surfaceHover().darker(107).name())));
    QVERIFY2(colorClose(pressedColor, theme->surfaceHover().darker(114)),
             qPrintable(QStringLiteral("neutralPressedColor=%1 expected=%2")
                            .arg(pressedColor.name(),
                                 theme->surfaceHover().darker(114).name())));

    // 核心方向断言：按下比 hover 更深，不能越按越亮。
    const double hoverLuma = relativeLuminance(hoverColor);
    const double pressedLuma = relativeLuminance(pressedColor);
    QVERIFY2(pressedLuma < hoverLuma,
             qPrintable(QStringLiteral("pressed luminance %1 must be lower than hover %2")
                            .arg(pressedLuma)
                            .arg(hoverLuma)));

    // 实际背景在 down=true 时使用 neutralPressedColor（而非亮色更浅的
    // controlBackgroundPressed），释放后回到 surfaceHover。
    QCOMPARE(propertyColor(background, "color"), theme->surfaceHover());
    button->setProperty("down", true);
    QCoreApplication::processEvents();
    QCOMPARE(propertyColor(background, "color"), pressedColor);
    QVERIFY2(relativeLuminance(propertyColor(background, "color")) < hoverLuma,
             "pressed background must be darker than the hover background");
    button->setProperty("down", false);
    QCoreApplication::processEvents();
    QCOMPARE(propertyColor(background, "color"), theme->surfaceHover());
}

// 任务 #13：Slider 禁用时轨道与已填充段必须可区分。
void TestStyleQml::sliderDisabledContrast()
{
    auto* theme = NexusAI::ThemeProvider::instance();
    QVERIFY(theme);

    Harness harness;
    QVERIFY2(harness.load(QStringLiteral("StyleHarness.qml")),
             qPrintable(harness.error()));
    QQuickWindow* window = harness.window();
    QVERIFY(window);

    QObject* slider = findObject(window, "slider");
    QVERIFY(slider);
    QQuickItem* background = qobject_cast<QQuickItem*>(backgroundOf(slider));
    QVERIFY(background);

    const QList<QQuickItem*> children = background->childItems();
    QVERIFY2(!children.isEmpty(),
             "Slider background must contain the filled (progress) segment");
    QObject* filled = children.first();
    QVERIFY(filled);

    // 启用态：轨道 borderSubtle、已填充段 brandPrimary。
    QCOMPARE(propertyColor(background, "color"), theme->borderSubtle());
    QCOMPARE(propertyColor(filled, "color"), theme->brandPrimary());

    slider->setProperty("enabled", false);
    QCoreApplication::processEvents();

    const QColor trackColor = propertyColor(background, "color");
    const QColor filledColor = propertyColor(filled, "color");
    QCOMPARE(trackColor, theme->controlBorderDisabled());
    // 修复 #11 的核心：填充段用更深的 controlTextDisabled，而不是与轨道同色。
    QCOMPARE(filledColor, theme->controlTextDisabled());
    QVERIFY2(!colorClose(trackColor, filledColor),
             qPrintable(QStringLiteral("disabled track %1 must differ from filled %2")
                            .arg(trackColor.name(), filledColor.name())));

    // 恢复启用态，避免影响同进程中后续用例。
    slider->setProperty("enabled", true);
    QCoreApplication::processEvents();
    QCOMPARE(propertyColor(background, "color"), theme->borderSubtle());
    QCOMPARE(propertyColor(filled, "color"), theme->brandPrimary());
}

int main(int argc, char* argv[])
{
    // 主题单例会读偏好；先把 QSettings 重定向到临时 INI，避免测试改写用户配置。
    NexusAI::TestSupport::SettingsIsolation isolation;
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QGuiApplication app(argc, argv);

    // 必须在 QML 引擎/QQmlComponent 之前选中自定义样式：Qt Quick Controls
    // 在控件实例化时按样式名解析 :/qt/qml/NexusStyle/<Control>.qml。
    QQuickStyle::setStyle(QStringLiteral("NexusStyle"));
    // 基线口径为暗色（docs/44 §11 的实测值来自暗色主题）。
    NexusAI::ThemeManager::instance()->setThemeMode(QStringLiteral("dark"));

    TestStyleQml testCase;
    return QTest::qExec(&testCase, argc, argv);
}

#include "test_style_qml.moc"