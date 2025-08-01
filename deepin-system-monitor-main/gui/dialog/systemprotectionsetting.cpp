// Copyright (C) 2011 ~ 2021 Uniontech Software Technology Co.,Ltd
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "systemprotectionsetting.h"
#include "helper.hpp"
#include "ddlog.h"
#include <DSettingsWidgetFactory>
#include <DCommandLinkButton>
#include <DSwitchButton>
#include <DLineEdit>
#include <DPalette>
#include <DLabel>

#include <QStandardPaths>
#include <QApplication>
#include <DSettingsOption>
#include <QDebug>
#include <QDBusInterface>
#include <QProcess>
#include <QTimer>
#include <QDBusConnection>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QIntValidator>
#include <QFont>

#ifndef AlarmStatusOptionName
#    define AlarmStatusOptionName "setting.systemprotection.alarm_switch"
#    define AlarmCpuUsageOptionName "setting.systemprotection.alarm_cpu_usage"
#    define AlarmMemUsageOptionName "setting.systemprotection.alarm_memory_usage"
#    define AlarmIntervalOptionName "setting.systemprotection.alarm_interval"
#    define AlarmLastTimeOptionName "setting.systemprotection.alarm_last_time"
#endif

// 与系统字体大小的偏移值，-1.5为小于系统字体大小1.5字号
static const double ToolTipFontSizeDiff = -1.5;
int SystemProtectionSetting::m_lastValidCPUValue = 0;
int SystemProtectionSetting::m_lastValidMemoryValue = 0;
int SystemProtectionSetting::m_lastValidInternalValue = 0;

Q_GLOBAL_STATIC(SystemProtectionSetting, theInstance)
using namespace DDLog;
// 修改控件字体大小
bool changeWidgetFontSizeByDiffWithSystem(QWidget *widget, double diff)
{
    qCDebug(app) << "changeWidgetFontSizeByDiffWithSystem";
    // 无效参数, 返回
    if (widget == nullptr || diff == 0.0) {
        qCWarning(app) << "Invalid widget or diff value";
        return false;
    }

    // 获取系统字体大小设置
    QDBusInterface interface(common::systemInfo().AppearanceService,
                             common::systemInfo().AppearancePath,
                             common::systemInfo().AppearanceInterface);
    // 获取失败，返回
    if (interface.isValid() == false) {
        qCWarning(app) << "Failed to create DBus interface for appearance service";
        return false;
    }

    // 获取系统字体大小
    double sysFontSize = interface.property("FontSize").toDouble();

    // 获取系统字体大小非法 或者 调整后值非法， 返回
    if (sysFontSize == 0.0 || sysFontSize + diff <= 0) {
        qCWarning(app) << "Invalid system font size:" << sysFontSize << "or adjusted size:" << (sysFontSize + diff);
        return false;
    }

    // 获得当前字体
    qCDebug(app) << "Changing widget font size, system font size:" << sysFontSize << "diff:" << diff;
    QFont font = widget->font();
    // 重设字体大小
    font.setPointSizeF(sysFontSize + diff);
    // 为 widget 设置字体
    widget->setFont(font);

    return true;
}

SystemProtectionSetting::SystemProtectionSetting(QObject *parent)
    : QObject(parent), mBackend(nullptr), mDsettings(nullptr), mDaemonInterface("org.deepin.SystemMonitorDaemon", "/org/deepin/SystemMonitorDaemon", "org.deepin.SystemMonitorDaemon")
{
    qCDebug(app) << "SystemProtectionSetting constructor";
    mDaemonInterface.setParent(this);
    // json文件加载setting基本结构
    mDsettings = DSettings::fromJsonFile(":/resources/settings.json");
    // 更新数据后台并同步数据
    onUpdateNewBackend();
    // 连接设置数据更改信号
    connect(mDsettings, &DSettings::valueChanged, this, &SystemProtectionSetting::onSettingItemChanged);
    // 跳转通知按钮单独处理
    connect(mDsettings->option("setting.messgae.message_setting"), &Dtk::Core::DSettingsOption::valueChanged,
            this, &SystemProtectionSetting::onMessgaeSetting);
}

SystemProtectionSetting::~SystemProtectionSetting()
{
    qCDebug(app) << "SystemProtectionSetting destructor";
    if (mDsettings != nullptr) {
        qCDebug(app) << "Deleting mDsettings";
        mDsettings->deleteLater();
    }

    if (mBackend != nullptr) {
        qCDebug(app) << "Deleting mBackend";
        mBackend->deleteLater();
    }
}

// 返回全局单例
SystemProtectionSetting *SystemProtectionSetting::instance()
{
    qCDebug(app) << "SystemProtectionSetting instance";
    return theInstance;
}

QPair<QWidget *, QWidget *> SystemProtectionSetting::createSettingLinkButtonHandle(QObject *obj)
{
    qCDebug(app) << "createSettingLinkButtonHandle";
    auto option = qobject_cast<DTK_CORE_NAMESPACE::DSettingsOption *>(obj);

    // 构建自定义Item
    QWidget *widget = new QWidget();
    QHBoxLayout *layout = new QHBoxLayout(widget);
    layout->addStretch();
    DCommandLinkButton *button = new DCommandLinkButton(tr("Settings"), widget);
    // 设置按钮字体大小，比系统标准字体小1.5号
    changeWidgetFontSizeByDiffWithSystem(button, ToolTipFontSizeDiff);
    layout->addWidget(button);
    widget->setLayout(layout);

    QPair<QWidget *, QWidget *> optionWidget = DSettingsWidgetFactory::createStandardItem(QByteArray(), option, widget);
    // 获取初始值
    option->setValue(option->defaultValue());

    option->connect(button, &DCommandLinkButton::clicked, option, [=]() {
        qCDebug(app) << "Settings button clicked, jumping to DDE setting center notification page";
        SystemProtectionSetting::instance()->onMessgaeSetting("");
    });
    if (widget != nullptr)
        widget->deleteLater();
    return optionWidget;
}

QPair<QWidget *, QWidget *> SystemProtectionSetting::createProtectionSwitchHandle(QObject *obj)
{
    qCDebug(app) << "createProtectionSwitchHandle";
    auto option = qobject_cast<DTK_CORE_NAMESPACE::DSettingsOption *>(obj);

    // 构建自定义Item
    QWidget *widget = new QWidget();
    QHBoxLayout *layout = new QHBoxLayout(widget);
    DLabel *label = new DLabel(widget);
    // 设置提示语为DTK提示语颜色
    label->setForegroundRole(DPalette::ColorType::TextTips);
    // 设置提示语字体大小，比系统标准字体小1.5号
    changeWidgetFontSizeByDiffWithSystem(label, ToolTipFontSizeDiff);
    // 设置提示语文本
    label->setText(tr("(Modifiable)"));
    layout->addWidget(label);
    layout->addStretch();

    DSwitchButton *button = new DSwitchButton(widget);
    layout->addWidget(button);
    widget->setLayout(layout);

    // 获取初始值
    button->setChecked(option->value().toBool());

    QPair<QWidget *, QWidget *> optionWidget = DSettingsWidgetFactory::createStandardItem(QByteArray(), option, widget);
    // Item操作修改Setting值
    option->connect(button, &DSwitchButton::clicked, option, [=](bool checked) {
        qCDebug(app) << "Protection switch changed to:" << checked;
        if (checked != option->value().toBool()) {
            option->setValue(checked);
        }
    });

    // 用于恢复默认时，Item的数据更新
    button->connect(option, &DSettingsOption::valueChanged, button, [=]() {
        qCDebug(app) << "Protection switch value changed, updating button state";
        if (option->value().toBool() != button->isChecked()) {
            button->setChecked(option->value().toBool());
        }
    });

    if (widget != nullptr)
        widget->deleteLater();
    return optionWidget;
}

void SystemProtectionSetting::setLastValidAlarm(DLineEdit *lineEdit, DTK_CORE_NAMESPACE::DSettingsOption *option, int maxValue, int minValue, int num)
{
    qCDebug(app) << "setLastValidAlarm";
    if (num >= minValue && num <= maxValue) {
        qCDebug(app) << "setLastValidAlarm, num:" << num;
        lineEdit->setText(QString::number(num));
        option->setValue(num);
    } else {
        qCDebug(app) << "setLastValidAlarm, num is invalid, num:" << num;
        //用上次的合法值设置
        if (option->key() == AlarmCpuUsageOptionName)
            lineEdit->setText(QString::number(m_lastValidCPUValue));
        else if (option->key() == AlarmMemUsageOptionName)
            lineEdit->setText(QString::number(m_lastValidMemoryValue));
        else if (option->key() == AlarmIntervalOptionName)
            lineEdit->setText(QString::number(m_lastValidInternalValue));
    }
}

void SystemProtectionSetting::lineEditChanged(bool focus, DLineEdit *edit, DSettingsOption *option, int maxValue, int minValue)
{
    QString key = option->key();
    qCDebug(app) << "Line edit changed for option:" << key << "focus:" << focus;
    if (focus == false && edit->lineEdit()->selectedText().isEmpty()) {
        qCDebug(app) << "Line edit changed for option:" << key << "focus:" << focus << "text:" << edit->text();
        if (edit->text().isEmpty() || edit->text().toInt() < minValue || edit->text().toInt() > maxValue) {
            qCWarning(app) << "Invalid input value:" << edit->text() << "for option:" << key;
            //如果上次设置值合法，当前输入值不合法，显示上次输入的合法值
            if (key == AlarmCpuUsageOptionName)
                setLastValidAlarm(edit, option, maxValue, minValue, m_lastValidCPUValue);
            else if (key == AlarmMemUsageOptionName) {
                setLastValidAlarm(edit, option, maxValue, minValue, m_lastValidMemoryValue);
            } else if (key == AlarmIntervalOptionName) {
                setLastValidAlarm(edit, option, maxValue, minValue, m_lastValidInternalValue);
            }
        }
        //当前输入合法
        if (key == AlarmCpuUsageOptionName) {
            m_lastValidCPUValue = edit->text().toInt();
            option->setValue(m_lastValidCPUValue);
        } else if (key == AlarmMemUsageOptionName) {
            m_lastValidMemoryValue = edit->text().toInt();
            option->setValue(m_lastValidMemoryValue);
        } else if (key == AlarmIntervalOptionName) {
            m_lastValidInternalValue = edit->text().toInt();
            option->setValue(m_lastValidInternalValue);
        }
    }
}

QPair<QWidget *, QWidget *> SystemProtectionSetting::createAlarmUsgaeSettingHandle(QObject *obj)
{
    qCDebug(app) << "createAlarmUsgaeSettingHandle";
    auto option = qobject_cast<DTK_CORE_NAMESPACE::DSettingsOption *>(obj);

    // 构建自定义Item
    const int lineEditWidth = 60;
    const int lineEditHeight = 36;
    const int labelWidth = 300;
    const int widgetSpace = 15;

    QWidget *widget = new QWidget;
    QHBoxLayout *layout = new QHBoxLayout(widget);
    // 构建编辑框
    DLineEdit *edit = new DLineEdit(widget);
    edit->setFixedSize(lineEditWidth, lineEditHeight);
    edit->setClearButtonEnabled(false);
    // 为编辑框设置数字输入限制
    QIntValidator *validator = new QIntValidator(edit);
    validator->setRange(1, 100);
    edit->lineEdit()->setValidator(validator);

    // 构建提示语
    DLabel *label = new DLabel(widget);
    // 设置提示语字体大小，比系统标准字体小1.5号
    changeWidgetFontSizeByDiffWithSystem(label, ToolTipFontSizeDiff);
    // 设置提示语为DTK提示语颜色
    label->setForegroundRole(DPalette::ColorType::TextTips);

    // 设置提示语文本
    label->setText(QString(tr("Range: %1-%2")).arg(30).arg(100));
    label->setFixedWidth(labelWidth);

    // 构建布局
    layout->addWidget(edit);
    layout->addSpacing(widgetSpace);
    layout->addWidget(label);
    layout->addStretch();
    widget->setLayout(layout);

    // 获取初始值
    edit->setText(option->value().toString());
    // 获取初始值时同步更新静态变量
    if (option->key() == AlarmCpuUsageOptionName)
        m_lastValidCPUValue = option->value().toInt();
    else if (option->key() == AlarmMemUsageOptionName)
        m_lastValidMemoryValue = option->value().toInt();

    // 修改Item同步修改Setting数据
    option->connect(edit, &DLineEdit::focusChanged, option, [=](bool onFocus) {
        lineEditChanged(onFocus, edit, option, 100, 30);
    });

    option->connect(edit, &DLineEdit::textChanged, option, [=]() {
        QString text = edit->text();
        int number = text.toInt();
        edit->setText(QString::number(number));
    });

    option->connect(edit, &DLineEdit::returnPressed, option, [=]() {
        lineEditChanged(false, edit, option, 100, 30);
    });

    // 用于恢复默认时，Item的数据更新
    edit->connect(option, &DSettingsOption::valueChanged, edit, [=]() {
        if (option->value().toInt() != edit->text().toInt()) {
            edit->setText(option->value().toString());
            // 恢复默认时，需要更新默认设置值
            if (option->key() == AlarmCpuUsageOptionName)
                m_lastValidCPUValue = option->value().toInt();
            if (option->key() == AlarmMemUsageOptionName)
                m_lastValidMemoryValue = option->value().toInt();
        }
    });

    QPair<QWidget *, QWidget *> optionWidget = DSettingsWidgetFactory::createStandardItem(QByteArray(), option, widget);
    QWidget *leftWidget = optionWidget.first;

    // 获取setting指针
    auto settingPtr = SystemProtectionSetting::instance()->getDSettingPointor();
    if (settingPtr != nullptr) {
        // 初始当前监控开关状态
        QVariant swtich_value = settingPtr->getOption(AlarmStatusOptionName);
        // 初始控件的disable状态
        widget->setDisabled(!swtich_value.toBool());
        // 连接监控开关变化信号
        widget->connect(settingPtr, &DSettings::valueChanged, widget, [=](const QString &key, const QVariant &value) {
            if (key == AlarmStatusOptionName) {
                widget->setDisabled(!value.toBool());
            }
        });

        if (leftWidget != nullptr) {
            // 初始控件的disable状态
            leftWidget->setDisabled(!swtich_value.toBool());
            // 连接监控开关变化信号
            leftWidget->connect(settingPtr, &DSettings::valueChanged, leftWidget, [=](const QString &key, const QVariant &value) {
                if (key == AlarmStatusOptionName) {
                    leftWidget->setDisabled(!value.toBool());
                }
            });
        }
    }

    if (widget != nullptr)
        widget->deleteLater();
    return optionWidget;
}

QPair<QWidget *, QWidget *> SystemProtectionSetting::createAlarmIntervalSettingHandle(QObject *obj)
{
    qCDebug(app) << "createAlarmIntervalSettingHandle";
    auto option = qobject_cast<DTK_CORE_NAMESPACE::DSettingsOption *>(obj);

    // 构建自定义Item
    const int lineEditWidth = 60;
    const int lineEditHeight = 36;
    const int labelWidth = 300;
    const int widgetSpace = 15;

    QWidget *widget = new QWidget;
    QHBoxLayout *layout = new QHBoxLayout(widget);
    // 构建编辑框//
    DLineEdit *edit = new DLineEdit(widget);
    edit->setFixedSize(lineEditWidth, lineEditHeight);
    edit->setClearButtonEnabled(false);
    // 为编辑框设置数字输入限制
    QIntValidator *validator = new QIntValidator(edit);
    validator->setRange(1, 60);
    edit->lineEdit()->setValidator(validator);

    // 构建提示语
    DLabel *label = new DLabel(widget);
    // 设置提示语为DTK提示语颜色
    label->setForegroundRole(DPalette::ColorType::TextTips);
    // 设置提示语字体大小，比系统标准字体小1.5号
    changeWidgetFontSizeByDiffWithSystem(label, ToolTipFontSizeDiff);

    // 设置提示语文本
    label->setText(QString(tr("Range: %1-%2")).arg(5).arg(60));
    label->setFixedWidth(labelWidth);

    // 构建布局
    layout->addWidget(edit);
    layout->addSpacing(widgetSpace);
    layout->addWidget(label);
    layout->addStretch();
    widget->setLayout(layout);

    // 设定Item初始值//
    edit->setText(option->value().toString());

    // 获取初始值时同步更新静态变量
    m_lastValidInternalValue = option->value().toInt();
    // 修改Item同步修改Setting数据
    option->connect(edit, &DLineEdit::focusChanged, option, [=](bool onFocus) {
        lineEditChanged(onFocus, edit, option, 60, 5);
    });

    option->connect(edit, &DLineEdit::textChanged, option, [=]() {
        QString text = edit->text();
        int number = text.toInt();
        edit->setText(QString::number(number));
    });

    option->connect(edit, &DLineEdit::returnPressed, option, [=]() {
        lineEditChanged(false, edit, option, 60, 5);
    });

    // 用于恢复默认时，Item的数据更新
    edit->connect(option, &DSettingsOption::valueChanged, edit, [=]() {
        if (option->value().toInt() != edit->text().toInt()) {
            edit->setText(option->value().toString());
            if (option->key() == AlarmIntervalOptionName)
                m_lastValidInternalValue = option->value().toInt();
        }
    });

    QPair<QWidget *, QWidget *> optionWidget = DSettingsWidgetFactory::createStandardItem(QByteArray(), option, widget);
    QWidget *leftWidget = optionWidget.first;

    // 获取setting指针
    auto settingPtr = SystemProtectionSetting::instance()->getDSettingPointor();
    if (settingPtr != nullptr) {
        // 初始当前监控开关状态
        QVariant swtich_value = settingPtr->getOption(AlarmStatusOptionName);
        // 初始控件的disable状态
        widget->setDisabled(!swtich_value.toBool());
        // 连接监控开关变化信号
        widget->connect(settingPtr, &DSettings::valueChanged, widget, [=](const QString &key, const QVariant &value) {
            if (key == AlarmStatusOptionName) {
                widget->setDisabled(!value.toBool());
            }
        });

        if (leftWidget != nullptr) {
            // 初始控件的disable状态
            leftWidget->setDisabled(!swtich_value.toBool());
            // 连接监控开关变化信号
            leftWidget->connect(settingPtr, &DSettings::valueChanged, leftWidget, [=](const QString &key, const QVariant &value) {
                if (key == AlarmStatusOptionName) {
                    leftWidget->setDisabled(!value.toBool());
                }
            });
        }
    }

    if (widget != nullptr)
        widget->deleteLater();
    return optionWidget;
}

void SystemProtectionSetting::onMessgaeSetting(QVariant value)
{
    qCDebug(app) << "onMessgaeSetting";
    Q_UNUSED(value);
    QString genericName;
    QFile desktopFile("/usr/share/applications/deepin-system-monitor.desktop");

    // 解析desktop文件，获取当前 GenericName
    if (desktopFile.exists() && desktopFile.open(QFile::ReadOnly)) {
        qCDebug(app) << "desktopFile exists";
        QLocale locale;
        QString searchString;
        searchString = QString("GenericName[%1]").arg(locale.name());

        while (desktopFile.atEnd() == false) {
            QString line = desktopFile.readLine();
            if (line.left(searchString.size()) == searchString) {
                genericName = line.mid(searchString.size() + 1);
                genericName = genericName.trimmed();
                break;
            }
        }
        desktopFile.close();
    }

    if (genericName.isEmpty() == false) {
        qCDebug(app) << "Found generic name:" << genericName << "opening specific notification page";
        // 跳转到设置页并指定Item
        QDBusMessage showDDEControlCenterPage = QDBusMessage::createMethodCall(common::systemInfo().ControlCenterService,
                                                                               common::systemInfo().ControlCenterPath,
                                                                               common::systemInfo().ControlCenterInterface,
                                                                               "ShowPage");
        QList<QVariant> args;
        if (!common::systemInfo().isOldVersion()) {
            args << QString("notification/%1").append(genericName);
        } else {
            args.append("notification");
            args.append(genericName);
        }

        showDDEControlCenterPage.setArguments(args);

        QDBusMessage replyMsg = QDBusConnection::sessionBus().call(showDDEControlCenterPage);

        if (replyMsg.type() == QDBusMessage::ErrorMessage) {
            qCWarning(app) << "Failed to call DDE control center DBus method, error name:" 
                       << replyMsg.errorName() << "error message:" << replyMsg.errorMessage();
        }

    } else {
        qCDebug(app) << "No generic name found, opening general notification page";
        // 跳转到设置页
        QDBusMessage showDDEControlCenter;
        if (!common::systemInfo().isOldVersion()) {
            showDDEControlCenter = QDBusMessage::createMethodCall(common::systemInfo().ControlCenterService,
                                                                  common::systemInfo().ControlCenterPath,
                                                                  common::systemInfo().ControlCenterInterface,
                                                                  "ShowPage");
        } else {
            showDDEControlCenter = QDBusMessage::createMethodCall("com.deepin.dde.ControlCenter",
                                                                  "/com/deepin/dde/ControlCenter",
                                                                  "com.deepin.dde.ControlCenter",
                                                                  "ShowModule");
        }

        QList<QVariant> args;
        args << "notification";
        showDDEControlCenter.setArguments(args);

        QDBusMessage replyMsg = QDBusConnection::sessionBus().call(showDDEControlCenter);

        if (replyMsg.type() == QDBusMessage::ErrorMessage) {
            qCWarning(app) << "Failed to call DDE control center DBus method, error name:" 
                       << replyMsg.errorName() << "error message:" << replyMsg.errorMessage();
        }
    }
}

void SystemProtectionSetting::onSettingItemChanged(const QString &key, const QVariant &value)
{
    qCDebug(app) << "Setting item changed - key:" << key << "value:" << value;
    // 使用QProcess执行dbus操作， 此操作是为了通知daemon进程，监测功能设置数据更改
    // 在执行 DSettingsDialog "恢复默认"按钮操作时，QDBusInterFace QDBusConnection 等操作会发生调用错误
    // 错误提示 org.freedesktop.DBus.Error.UnknownMethod
    // 采用QProcess执行dbus操作，不会发生此项报错，dbus调用正常

    //QString cmd("qdbus org.deepin.SystemMonitorDaemon /org/deepin/SystemMonitorDaemon org.deepin.SystemMonitorDaemon.");
    //qdbus 改为gdbus
    QString program = "gdbus";
    QStringList arguments;
    arguments << "call" << "-e" << "-d" << "org.deepin.SystemMonitorDaemon" 
              << "-o" << "/org/deepin/SystemMonitorDaemon" 
              << "-m" << "org.deepin.SystemMonitorDaemon.";

    // 根据不同的key添加对应的方法名和参数
    if (key == AlarmStatusOptionName) {
        arguments.last().append("setSystemProtectionStatus");
        arguments << value.toString();
    } else if (key == AlarmCpuUsageOptionName) {
        arguments.last().append("setAlarmUsageOfCpu"); 
        arguments << value.toString();
    } else if (key == AlarmMemUsageOptionName) {
        arguments.last().append("setAlarmUsageOfMemory");
        arguments << value.toString();
    } else if (key == AlarmIntervalOptionName) {
        arguments.last().append("setAlarmMsgInterval");
        arguments << value.toString();
    } else if (key == AlarmLastTimeOptionName) {
        arguments.last().append("setAlarmLastTimeInterval");
        arguments << value.toString();
    } else {
        qCWarning(app) << "Unknown setting key:" << key;
        return;
    }

    QTimer::singleShot(100, this, [=]() {
        qCDebug(app) << "Executing DBus command:" << program << arguments;
        QProcess process;
        process.start(program, arguments);
        process.waitForFinished(5000);
    });
}

DSettings *SystemProtectionSetting::getDSettingPointor()
{
    // qCDebug(app) << "getDSettingPointor";
    return mDsettings;
}

void SystemProtectionSetting::regularNumber(DLineEdit *lineEdit)
{
    qCDebug(app) << "regularNumber";
    // 失去焦点时
    connect(lineEdit, &DLineEdit::focusChanged, SystemProtectionSetting::instance(), [=]() {
        if (lineEdit) {
            QString text = lineEdit->text();
            int number = text.toInt();
            lineEdit->setText(QString::number(number));
        }
    });

    // 回车按下时
    connect(lineEdit, &DLineEdit::returnPressed, SystemProtectionSetting::instance(), [=]() {
        if (lineEdit) {
            QString text = lineEdit->text();
            int number = text.toInt();
            lineEdit->setText(QString::number(number));
        }
    });
}

void SystemProtectionSetting::onUpdateNewBackend()
{
    qCDebug(app) << "onUpdateNewBackend";
    // 创建新的数据后端，应对可能的设置数据变化
    QString strConfigPath = QString("%1/%2/%3/protection.conf")
                                    .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
                                    .arg(qApp->organizationName())
                                    .arg(qApp->applicationName());

    qCDebug(app) << "Using config path:" << strConfigPath;
    Dtk::Core::QSettingBackend *newBackend = new QSettingBackend(strConfigPath);
    // 为DSetting更新数据后端
    mDsettings->setBackend(newBackend);
    mDsettings->sync();

    // 销毁原来的数据后端
    if (mBackend != nullptr) {
        mBackend->deleteLater();
    }

    // 记录新的数据后端指针
    mBackend = newBackend;
}
