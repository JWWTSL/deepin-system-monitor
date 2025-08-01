// Copyright (C) 2019 ~ 2021 Uniontech Software Technology Co.,Ltd.
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "base_detail_view_widget.h"
#include "../detail_view_stacked_widget.h"
#include "base_commandlink_button.h"
#include "gui/ui_common.h"
#include "ddlog.h"

#include <QPainter>
#include <DApplication>
#include <DFontSizeManager>
#include <QKeyEvent>
#include <DPushButton>

DWIDGET_USE_NAMESPACE
using namespace DDLog;

BaseDetailViewWidget::BaseDetailViewWidget(QWidget *parent) : QWidget(parent)
{
    qCDebug(app) << "BaseDetailViewWidget constructor";
    m_centralLayout = new QVBoxLayout(this);
    m_centralLayout->setContentsMargins(16, 10, 16, 16);
    m_centralLayout->setSpacing(16);

    DetailViewStackedWidget *stackViewWidget = dynamic_cast<DetailViewStackedWidget *>(parent);
    m_detailButton = new BaseCommandLinkButton(tr("Hide details"), this);
    DFontSizeManager::instance()->bind(m_detailButton, DFontSizeManager::T8, QFont::Medium);
    connect(m_detailButton, &BaseCommandLinkButton::clicked, stackViewWidget, &DetailViewStackedWidget::onSwitchProcessPage);

    m_switchButton = new DIconButton(DStyle::SP_ReduceElement, this);
    m_switchButton->setIconSize(QSize(12, 12));
    m_switchButton->setFixedSize(30, 30);
    m_switchButton->setEnabledCircle(true);
    m_switchIconDark = new QIcon(iconPathFromQrc("dark/change_dark.svg"));
    m_switchIconLight = new QIcon(iconPathFromQrc("light/change_light.svg"));
    if (m_isMultiCoreMode) {
        m_switchButton->setToolTip(tr("Overall utilization"));
        qCDebug(app) << "Setting initial tooltip to 'Overall utilization'";
    } else {
        m_switchButton->setToolTip(tr("Individual utilization"));
        qCDebug(app) << "Setting initial tooltip to 'Individual utilization'";
    }

    connect(m_switchButton, &DIconButton::clicked, [ = ]() {
        m_isMultiCoreMode = !m_isMultiCoreMode;
        qCDebug(app) << "Switching core mode to:" << (m_isMultiCoreMode ? "Multi-core" : "Single-core");
        emit sigClickSwitchMutliCoreButton(m_isMultiCoreMode);
        if (m_isMultiCoreMode) {
            m_switchButton->setToolTip(tr("Overall utilization"));
        } else {
            m_switchButton->setToolTip(tr("Individual utilization"));
        }
    });

    m_arrowButton = new DIconButton(DStyle::SP_ReduceElement, this);
    m_arrowButton->setIconSize(QSize(10, 10));
    m_arrowButton->setFixedSize(24, 24);
    m_arrowButton->setEnabledCircle(true);
    connect(m_arrowButton, &DIconButton::clicked, [ = ]() {
        qCDebug(app) << "Showing performance menu";
        stackViewWidget->onShowPerformMenu(m_arrowButton->mapToGlobal(QPoint(0, m_arrowButton->height() + 6)));
    });

    connect(stackViewWidget, &DetailViewStackedWidget::signalIsFinished, this, [ = ]() {
        QString currentWidgetName = stackViewWidget->currentWidget()->objectName();
        qCDebug(app) << "Current widget changed to:" << currentWidgetName;
        //CPUDetailWidget MemDetailViewWidget NetifDetailViewWidget BlockDevDetailViewWidget
        if (currentWidgetName == "CPUDetailWidget") {
            m_switchButton->setVisible(true);
            qCDebug(app) << "Showing switch button for CPU detail view";
        } else {
            m_switchButton->setVisible(false);
            qCDebug(app) << "Hiding switch button for non-CPU detail view";
        }
    });

    detailFontChanged(DApplication::font());
    connect(dynamic_cast<QGuiApplication *>(DApplication::instance()), &DApplication::fontChanged,
            this, &BaseDetailViewWidget::detailFontChanged);

    // adjust search result tip label text color dynamically on theme type change
    onThemeTypeChanged(DGuiApplicationHelper::instance()->themeType());
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    connect(DApplicationHelper::instance(), &DApplicationHelper::themeTypeChanged, this, &BaseDetailViewWidget::onThemeTypeChanged);
#else
    connect(DGuiApplicationHelper::instance(), &DGuiApplicationHelper::themeTypeChanged, this, &BaseDetailViewWidget::onThemeTypeChanged);
#endif
    // 当前的策略是为了解决页面切换时，焦点停留在按钮上，这样设置不会影响代码的逻辑
    if (stackViewWidget)
        stackViewWidget->setFocus();
}

BaseDetailViewWidget::~BaseDetailViewWidget()
{
    qCDebug(app) << "BaseDetailViewWidget destructor";
    if (m_switchIconDark) {
        qCDebug(app) << "Deleting m_switchIconDark";
        delete m_switchIconDark;
        m_switchIconDark = nullptr;
    }

    if (m_switchIconLight) {
        qCDebug(app) << "Deleting m_switchIconLight";
        delete m_switchIconLight;
        m_switchIconLight = nullptr;
    }
}

void BaseDetailViewWidget::detailFontChanged(const QFont &font)
{
    qCDebug(app) << "detailFontChanged";
    m_titleFont = font;
    m_titleFont.setPointSizeF(m_titleFont.pointSizeF() + 12);

    m_contentFont = font;
    m_contentFont.setPointSizeF(m_contentFont.pointSizeF() - 1);

    updateWidgetGrometry();
}

int BaseDetailViewWidget::titleHeight()
{
    // qCDebug(app) << "titleHeight";
    return QFontMetrics(m_titleFont).height();
}

void BaseDetailViewWidget::setTitle(const QString &text)
{
    qCDebug(app) << "Setting title to:" << text;
    m_titleText = text;
    update();
}

QString BaseDetailViewWidget::title()
{
    // qCDebug(app) << "Getting title:" << m_titleText;
    return m_titleText;
}

void BaseDetailViewWidget::setDetail(const QString &text)
{
    qCDebug(app) << "Setting detail text to:" << text;
    m_detailText = text;
    update();
}

QString BaseDetailViewWidget::detail()
{
    // qCDebug(app) << "Getting detail text:" << m_detailText;
    return m_detailText;
}

void BaseDetailViewWidget::updateWidgetGrometry()
{
    qCDebug(app) << "updateWidgetGrometry";
    QFontMetrics titleFont(m_titleFont);
    const QMargins &margin = m_centralLayout->contentsMargins();
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    m_arrowButton->setGeometry(margin.left() + titleFont.width(m_titleText) + 6, 10 + titleFont.height() / 2 - m_arrowButton->height() / 2, m_arrowButton->width(), m_arrowButton->height());
    m_detailButton->setFixedSize(m_detailButton->fontMetrics().width(m_detailButton->text()) + 16, m_detailButton->fontMetrics().height() + 4);
    const QSize &detailtextSize =  m_detailButton->size();
#else
    m_arrowButton->setGeometry(margin.left() + titleFont.horizontalAdvance(m_titleText) + 6, 10 + titleFont.height() / 2 - m_arrowButton->height() / 2, m_arrowButton->width(), m_arrowButton->height());
    m_detailButton->setFixedSize(m_detailButton->fontMetrics().horizontalAdvance(m_detailButton->text()) + 16, m_detailButton->fontMetrics().height() + 4);
    const QSize &detailtextSize =  m_detailButton->size();
#endif 

    m_detailButton->setGeometry(this->width() - detailtextSize.width() - 6, 10 + titleFont.height() / 2 - detailtextSize.height() / 2, detailtextSize.width(), detailtextSize.height());
    m_switchButton->setGeometry(m_detailButton->x() - 30, 10 + titleFont.height() / 2 - m_switchButton->height() / 2, m_switchButton->width(), m_switchButton->height());
}

void BaseDetailViewWidget::onThemeTypeChanged(DGuiApplicationHelper::ColorType themeType)
{
    qCDebug(app) << "onThemeTypeChanged, themeType:" << themeType;
    if (themeType == DGuiApplicationHelper::DarkType) {
        qCDebug(app) << "Dark theme";
        m_switchButton->setIcon(*m_switchIconDark);
    } else if (themeType == DGuiApplicationHelper::LightType) {
        qCDebug(app) << "Light theme";
        m_switchButton->setIcon(*m_switchIconLight);
    }
}

void BaseDetailViewWidget::resizeEvent(QResizeEvent *event)
{
    qCDebug(app) << "resizeEvent";
    QWidget::resizeEvent(event);
    updateWidgetGrometry();
}

void BaseDetailViewWidget::paintEvent(QPaintEvent *event)
{
    // qCDebug(app) << "paintEvent";
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    auto *dAppHelper = DApplicationHelper::instance();
#else
    auto *dAppHelper = DGuiApplicationHelper::instance();
#endif
    auto palette = dAppHelper->applicationPalette();

    painter.setPen(Qt::NoPen);
    painter.setBrush(palette.color(DPalette::Base));
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    painter.drawRoundRect(this->rect(), 2, 2);
#else
    painter.drawRoundedRect(this->rect(), 2, 2);
#endif

    painter.setFont(m_titleFont);
    painter.setPen(palette.color(DPalette::Text));
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QRect titleRect(m_centralLayout->contentsMargins().left(), 10, painter.fontMetrics().width(m_titleText), painter.fontMetrics().height());
#else
    QRect titleRect(m_centralLayout->contentsMargins().left(), 10, painter.fontMetrics().horizontalAdvance(m_titleText), painter.fontMetrics().height());
#endif
    painter.drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter, m_titleText);
    m_centralLayout->setContentsMargins(16, titleRect.height() + 10, 16, 16);

    if (!m_detailText.isEmpty()) {
        painter.setFont(m_contentFont);
        painter.setPen(palette.color(DPalette::TextTips));
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        QRect detailRect(m_arrowButton->width() + titleRect.right() + 12, titleRect.y(), painter.fontMetrics().width(m_detailText), titleRect.height());
#else
        QRect detailRect(m_arrowButton->width() + titleRect.right() + 12, titleRect.y(), painter.fontMetrics().horizontalAdvance(m_detailText), titleRect.height());
#endif
        painter.drawText(detailRect, Qt::AlignLeft | Qt::AlignVCenter, m_detailText);
    }
}

void BaseDetailViewWidget::mouseMoveEvent(QMouseEvent *event)
{
    // qCDebug(app) << "mouseMoveEvent";
    Q_UNUSED(event)
    //not Todo
}
