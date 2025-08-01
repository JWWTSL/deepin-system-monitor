// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ddlog.h"
#include "main_window.h"
#include "user_page_widget.h"
#include "application.h"
#include "process_page_widget.h"
#include "system_service_page_widget.h"
#include "toolbar.h"
#include "common/common.h"
#include "settings.h"
#include "common/perf.h"
#include "detailwidgetmanager.h"
#include "gui/dialog/systemprotectionsetting.h"
#include "process/process_set.h"
#include "common/eventlogutils.h"

#include <DSettingsWidgetFactory>
#include <DTitlebar>
#ifdef DTKCORE_CLASS_DConfigFile
#    include <DConfig>
#endif
#include <QKeyEvent>
#include <QTimer>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QDesktopWidget>
#endif
#include <QDBusConnection>
#include <QJsonObject>
#include <QActionGroup>

using namespace core::process;
using namespace common::init;
using namespace DDLog;
const int WINDOW_MIN_HEIGHT = 760;
const int WINDOW_MIN_WIDTH = 1080;

const QString SERVICE_NAME = "com.deepin.SystemMonitorMain";
const QString SERVICE_PATH = "/com/deepin/SystemMonitorMain";

MainWindow::MainWindow(QWidget *parent)
    : DMainWindow(parent), m_pDbusService(new DBusForSystemoMonitorPluginServce)
{
    qCDebug(app) << "MainWindow constructor";
    m_settings = Settings::instance();
    setMinimumSize(WINDOW_MIN_WIDTH, WINDOW_MIN_HEIGHT);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    setMaximumSize(QApplication::desktop()->size());
#else
    setMaximumSize(QGuiApplication::primaryScreen()->size());
#endif
    connect(this, &MainWindow::loadingStatusChanged, this, &MainWindow::onLoadStatusChanged);
#ifdef DTKCORE_CLASS_DConfigFile
    //需要查询是否支持特殊机型静音恢复，例如hw机型
    DConfig *dconfig = DConfig::create("org.deepin.system-monitor", "org.deepin.system-monitor.main");
    //需要判断Dconfig文件是否合法
    if (dconfig && dconfig->isValid() && dconfig->keyList().contains("specialComType")) {
        specialComType = dconfig->value("specialComType").toInt();
        qCDebug(app) << "Special computer type configured:" << specialComType;
    }
#endif
}

MainWindow::~MainWindow()
{
    qCDebug(app) << "MainWindow destructor";
    PERF_PRINT_END("POINT-02");
    if (m_pDbusService) {
        delete m_pDbusService;
        m_pDbusService = nullptr;
    }
}

void MainWindow::raiseWindow()
{
    qCDebug(app) << "raiseWindow";
    raise();
    activateWindow();
}

void MainWindow::initDisplay()
{
    qCDebug(app) << "initDisplay";
    QJsonObject obj {
        { "tid", EventLogUtils::Start },
        { "version", QCoreApplication::applicationVersion() },
        { "mode", 1 }
    };
    EventLogUtils::get().writeLogs(obj);

    initUI();
    initConnections();
}

void MainWindow::onLoadStatusChanged(bool loading)
{
    qCDebug(app) << "onLoadStatusChanged, loading:" << loading;
    titlebar()->setMenuDisabled(loading);
}

// initialize ui components
void MainWindow::initUI()
{
    qCDebug(app) << "initUI";
    // default window size
    int width = m_settings->getOption(kSettingKeyWindowWidth, WINDOW_MIN_WIDTH).toInt();
    int height = m_settings->getOption(kSettingKeyWindowHeight, WINDOW_MIN_HEIGHT).toInt();
    resize(width, height);

    // set titlebar icon
    titlebar()->setIcon(QIcon::fromTheme("deepin-system-monitor"));
    // new toolbar instance to add custom widgets
    m_toolbar = new Toolbar(this);
    // set toolbar focus policy to only accept tab focus
    m_toolbar->setFocusPolicy(Qt::TabFocus);
    titlebar()->setCustomWidget(m_toolbar);

    // custom menu to hold custom menu items
    DMenu *menu = new DMenu(this);
    titlebar()->setMenu(menu);

    // adjust toolbar tab order
    setTabOrder(titlebar(), m_toolbar);

    if (QDBusConnection::sessionBus().interface()->isServiceRegistered(QStringLiteral("org.kde.KWin"))) {
        qCDebug(app) << "KWin service is registered";
        // kill process menu item
        QAction *killAction = new QAction(DApplication::translate("Title.Bar.Context.Menu", "Force end application"), menu);
        // control + alt + k
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        killAction->setShortcut(QKeySequence(Qt::CTRL + Qt::ALT + Qt::Key_K));
#else
        killAction->setShortcut(QKeyCombination(Qt::CTRL | Qt::ALT, Qt::Key_K));
#endif
        // emit process kill requested signal if kill process menu item triggered
        connect(killAction, &QAction::triggered, this, [=]() { Q_EMIT killProcessPerformed(); });
        menu->addAction(killAction);
    }

    // display mode menu item
    DMenu *modeMenu = new DMenu(DApplication::translate("Title.Bar.Context.Menu", "View"), menu);
    QActionGroup *modeGroup = new QActionGroup(modeMenu);
    // set group items auto exclusive
    modeGroup->setExclusive(true);
    // expand mode sub menu item
    auto *expandModeAction = new QAction(DApplication::translate("Title.Bar.Context.Menu", "Expand"), modeGroup);
    expandModeAction->setCheckable(true);
    // compact mode sub menu item
    auto *compactModeAction = new QAction(DApplication::translate("Title.Bar.Context.Menu", "Compact"), modeGroup);
    compactModeAction->setCheckable(true);
    // add compact & expand sub menus into display mode menu as a group
    modeMenu->addAction(expandModeAction);
    modeMenu->addAction(compactModeAction);

    // load display mode backup setting
    int mode = m_settings->getOption(kSettingKeyDisplayMode).toInt();
    qCDebug(app) << "Loading display mode:" << mode;
    if (mode == kDisplayModeExpand) {
        qCDebug(app) << "Loading expand mode";
        expandModeAction->setChecked(true);
    } else if (mode == kDisplayModeCompact) {
        qCDebug(app) << "Loading compact mode";
        compactModeAction->setChecked(true);
    }

    // emit display mode changed signal if ether expand or compact menu item triggered
    connect(expandModeAction, &QAction::triggered, this, [=]() {
        qCDebug(app) << "Switching to expand mode";
        m_settings->setOption(kSettingKeyDisplayMode, kDisplayModeExpand);
        Q_EMIT displayModeChanged(kDisplayModeExpand);
    });
    connect(compactModeAction, &QAction::triggered, this, [=]() {
        qCDebug(app) << "Switching to compact mode";
        m_settings->setOption(kSettingKeyDisplayMode, kDisplayModeCompact);
        Q_EMIT displayModeChanged(kDisplayModeCompact);
    });

    // 等保需求，设置入口，1050打开
    // 构建setting menu Item Action
    QAction *settingAction(new QAction(tr("Settings"), this));
    connect(settingAction, &QAction::triggered, this, &MainWindow::popupSettingsDialog);

    menu->addSeparator();
    menu->addMenu(modeMenu);

    // 等保需求，设置入口，1050打开
    // 插入 setting 菜单项
    menu->addSeparator();
    menu->addAction(settingAction);
    menu->addSeparator();

    // stacked widget instance to hold process & service pages
    m_pages = new DStackedWidget(this);
    setCentralWidget(m_pages);
    setContentsMargins(0, 0, 0, 0);

    // shadow widget instance under titlebar
    m_tbShadow = new DShadowLine(m_pages);
    m_tbShadow->setFixedWidth(m_pages->width());
    m_tbShadow->setFixedHeight(10);
    m_tbShadow->move(0, 0);
    m_tbShadow->show();

    m_procPage = new ProcessPageWidget(m_pages);
    m_svcPage = new SystemServicePageWidget(m_pages);
    m_accountProcPage = new UserPageWidget(m_pages);

    m_pages->setContentsMargins(0, 0, 0, 0);
    m_pages->addWidget(m_procPage);
    m_pages->addWidget(m_svcPage);
    m_pages->addWidget(m_accountProcPage);
    m_tbShadow->raise();

    installEventFilter(this);
    qCDebug(app) << "MainWindow created";
}

// initialize connections
void MainWindow::initConnections()
{
    qCDebug(app) << "initConnections";
    connect(m_toolbar, &Toolbar::procTabButtonClicked, this, [=]() {
        PERF_PRINT_BEGIN("POINT-05", QString("switch(%1->%2)").arg(DApplication::translate("Title.Bar.Switch", "Services")).arg(DApplication::translate("Title.Bar.Switch", "Processes")));
        qCDebug(app) << "Switching to process page";
        m_toolbar->clearSearchText();
        m_pages->setCurrentWidget(m_procPage);

        m_tbShadow->raise();
        m_tbShadow->show();
        PERF_PRINT_END("POINT-05");
    });

    connect(m_toolbar, &Toolbar::serviceTabButtonClicked, this, [=]() {
        PERF_PRINT_BEGIN("POINT-05", QString("switch(%1->%2)").arg(DApplication::translate("Title.Bar.Switch", "Processes")).arg(DApplication::translate("Title.Bar.Switch", "Services")));
        qCDebug(app) << "Switching to service page";
        m_toolbar->clearSearchText();
        m_pages->setCurrentWidget(m_svcPage);

        m_tbShadow->raise();
        m_tbShadow->show();
        PERF_PRINT_END("POINT-05");
    });
    connect(m_toolbar, &Toolbar::accountProcTabButtonClicked, this, [=]() {
        PERF_PRINT_BEGIN("POINT-05", QString("switch(%1->%2)").arg(DApplication::translate("Title.Bar.Switch", "Users")).arg(DApplication::translate("Title.Bar.Switch", "Services")));
        qCDebug(app) << "Switching to account process page";
        m_toolbar->clearSearchText();
        m_pages->setCurrentWidget(m_accountProcPage);
        m_accountProcPage->onUserChanged();
        m_tbShadow->raise();
        m_tbShadow->show();
        PERF_PRINT_END("POINT-05");
    });
    connect(gApp, &Application::backgroundTaskStateChanged, this, [=](Application::TaskState state) {
        qCDebug(app) << "Background task state changed:" << state;
        if (state == Application::kTaskStarted) {
            qCDebug(app) << "Task started, disabling main window";
            // save last focused widget inside main window
            m_focusedWidget = gApp->mainWindow()->focusWidget();
            // disable main window (and any children widgets inside) if background process ongoing
            setEnabled(false);
        } else if (state == Application::kTaskFinished) {
            qCDebug(app) << "Task finished, re-enabling main window";
            // reenable main window after background process finished
            setEnabled(true);
            // restore focus to last focused widget if any
            if (m_focusedWidget && m_focusedWidget->isEnabled()) {
                m_focusedWidget->setFocus();
            }
        }
        gApp->setCurrentTaskState(state);
    });

    // 初始化DBUS
    if (QDBusConnection::sessionBus().isConnected()) {
        qCDebug(app) << "DBus session bus is connected";
        if (QDBusConnection::sessionBus().registerService(SERVICE_NAME)) {
            qCDebug(app) << "DBus session bus is registered";
            if (!QDBusConnection::sessionBus().registerObject(SERVICE_PATH, m_pDbusService, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) {
                qCWarning(app) << "Failed to register DBus object at path:" << SERVICE_PATH;
            } else {
                qCDebug(app) << "Successfully registered DBus service:" << SERVICE_NAME << "at path:" << SERVICE_PATH;
            }
        } else {
            qCWarning(app) << "Failed to register DBus service:" << SERVICE_NAME;
        }
    }

    connect(&DetailWidgetManager::getInstance(), &DetailWidgetManager::sigJumpToProcessWidget, this, &MainWindow::onDetailInfoByDbus, Qt::QueuedConnection);
    connect(&DetailWidgetManager::getInstance(), &DetailWidgetManager::sigJumpToDetailWidget, this, &MainWindow::onDetailInfoByDbus, Qt::QueuedConnection);

    connect(this, &MainWindow::killProcessPerformed, this, &MainWindow::onKillProcess);
}

// resize event handler
void MainWindow::resizeEvent(QResizeEvent *event)
{
    // qCDebug(app) << "MainWindow resized";
    // propogate resize event to base handler first
    DMainWindow::resizeEvent(event);
    m_tbShadow->setFixedWidth(this->width());
}

// close event handler
void MainWindow::closeEvent(QCloseEvent *event)
{
    // qCDebug(app) << "MainWindow close event";
    PERF_PRINT_BEGIN("POINT-02", "");
    // save new size to backup settings instace
    m_settings->setOption(kSettingKeyWindowWidth, width());
    m_settings->setOption(kSettingKeyWindowHeight, height());
    m_settings->flush();

    DMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    // qCDebug(app) << "MainWindow event filter";
    // handle key press events
    if (event->type() == QEvent::KeyPress) {
        auto *kev = dynamic_cast<QKeyEvent *>(event);
        if (kev->matches(QKeySequence::Quit)) {
            qCDebug(app) << "ESC pressed, quitting application";
            // ESC pressed
            DApplication::quit();
            return true;
        } else if (kev->matches(QKeySequence::Find)) {
            qCDebug(app) << "CTRL+F pressed, focusing search input";
            // CTRL + F pressed
            toolbar()->focusInput();
            return true;
        } else if ((kev->modifiers() == (Qt::ControlModifier | Qt::AltModifier)) && kev->key() == Qt::Key_K) {
            qCDebug(app) << "CTRL+ALT+K pressed, triggering kill process";
            // CTRL + ALT + K pressed
            Q_EMIT killProcessPerformed();
            return true;
        } else if ((kev->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) && kev->key() == Qt::Key_Question) {
            qCDebug(app) << "CTRL+SHIFT+? pressed, showing shortcut help";
            // CTRL + SHIFT + ? pressed
            common::displayShortcutHelpDialog(this->geometry());
            return true;
        }
    }
    return DMainWindow::eventFilter(obj, event);
}

void MainWindow::showEvent(QShowEvent *event)
{
    qCDebug(app) << "MainWindow show event";
    DMainWindow::showEvent(event);

    if (!m_initLoad) {
        qCDebug(app) << "MainWindow show event, starting monitor job";
        m_initLoad = true;
        QTimer::singleShot(5, this, SLOT(onStartMonitorJob()));
        PERF_PRINT_END("POINT-01");
    }
}

void MainWindow::onStartMonitorJob()
{
    qCDebug(app) << "onStartMonitorJob";
    auto *msev = new MonitorStartEvent();
    gApp->postEvent(gApp, msev);
    auto *netev = new NetifStartEvent();
    gApp->postEvent(gApp, netev);
}

void MainWindow::onDetailInfoByDbus(QString msgCode)
{
    qCDebug(app) << "onDetailInfoByDbus msgCode: " << msgCode;
    if (msgCode.compare(QString("MSG_PROCESS"), Qt::CaseInsensitive) == 0) {
        qCDebug(app) << "Switching to process page with no filter";
        m_toolbar->clearSearchText();
        m_toolbar->setProcessButtonChecked(true);
        m_pages->setCurrentWidget(m_procPage);
        m_procPage->switchProcessPage();
        m_procPage->switchCurrentNoFilterPage();
        m_tbShadow->raise();
        m_tbShadow->show();
        if (CPUPerformance == CPUMaxFreq::High) {
            qCDebug(app) << "CPUPerformance is High";
            m_settings->setOption(kSettingKeyProcessTabIndex, kNoFilter);
        } else {
            qCDebug(app) << "CPUPerformance is Low";
            m_settings->setOption(kSettingKeyProcessTabIndex, kFilterApps);
        }
    } else {
        qCDebug(app) << "Switching to process page";
        m_toolbar->clearSearchText();
        m_toolbar->setProcessButtonChecked(true);
        m_pages->setCurrentWidget(m_procPage);
        m_tbShadow->raise();
        m_tbShadow->show();
    }
}

void MainWindow::popupSettingsDialog()
{
    qCDebug(app) << "popupSettingsDialog";
    DSettingsDialog *dialog = new DSettingsDialog(this);
    // 注册自定义Item ， 为实现UI效果
    dialog->widgetFactory()->registerWidget("settinglinkbutton", SystemProtectionSetting::createSettingLinkButtonHandle);
    dialog->widgetFactory()->registerWidget("protectionswitch", SystemProtectionSetting::createProtectionSwitchHandle);
    dialog->widgetFactory()->registerWidget("cpualarmcritical", SystemProtectionSetting::createAlarmUsgaeSettingHandle);
    dialog->widgetFactory()->registerWidget("memalarmcritical", SystemProtectionSetting::createAlarmUsgaeSettingHandle);
    dialog->widgetFactory()->registerWidget("intervalalarmcritical", SystemProtectionSetting::createAlarmIntervalSettingHandle);

    // 连接Setting Dialog 和 Setting json 文件（后台根据json格式创建conf文件）
    SystemProtectionSetting::instance()->onUpdateNewBackend();
    dialog->updateSettings(SystemProtectionSetting::instance()->getDSettingPointor());

    // 显示Setting Dialog
    dialog->exec();

    // 销毁窗口
    dialog->deleteLater();
}

void MainWindow::onKillProcess()
{
    qCDebug(app) << "Kill process requested";
    if (QDBusConnection::sessionBus().interface()->isServiceRegistered(QStringLiteral("org.kde.KWin"))) {
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                    QStringLiteral("/KWin"),
                                                    QStringLiteral("org.kde.KWin"),
                                                    QStringLiteral("killWindow"));
        hide();
        QTimer::singleShot(300,this,[this](){
            qCDebug(app) << "Sending kill window request to KWin";
            auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                        QStringLiteral("/KWin"),
                                                        QStringLiteral("org.kde.KWin"),
                                                        QStringLiteral("killWindow"));
            QDBusConnection::sessionBus().asyncCall(message);
            QTimer::singleShot(1000,this,[this](){
                qCDebug(app) << "Showing window after kill request";
                show();
            });
        });
    } else {
        qCWarning(app) << "KWin service not available for kill process";
    }
}
