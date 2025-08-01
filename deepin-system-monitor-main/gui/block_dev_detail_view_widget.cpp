// Copyright (C) 2019 ~ 2020 Uniontech Software Technology Co.,Ltd
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "block_dev_detail_view_widget.h"
#include "block_dev_stat_view_widget.h"
#include "block_dev_summary_view_widget.h"
#include "ddlog.h"

#include <DApplication>

using namespace DDLog;

BlockDevDetailViewWidget::BlockDevDetailViewWidget(QWidget *parent)
    : BaseDetailViewWidget(parent)
{
    qCDebug(app) << "BlockDevDetailViewWidget constructor";
    this->setObjectName("BlockDevDetailViewWidget");

    setTitle(DApplication::translate("Process.Graph.View", "Disks"));
    m_blockStatWidget = new BlockStatViewWidget(this);
    m_blocksummaryWidget = new BlockDevSummaryViewWidget(this);
    m_centralLayout->addWidget(m_blockStatWidget);
    m_centralLayout->addWidget(m_blocksummaryWidget);
    connect(m_blockStatWidget, &BlockStatViewWidget::changeInfo, m_blocksummaryWidget, &BlockDevSummaryViewWidget::chageSummaryInfo);

    detailFontChanged(DApplication::font());
}

BlockDevDetailViewWidget::~BlockDevDetailViewWidget()
{
    qCDebug(app) << "BlockDevDetailViewWidget destructor";
}
void BlockDevDetailViewWidget::detailFontChanged(const QFont &font)
{
    qCDebug(app) << "BlockDevDetailViewWidget font changed";
    BaseDetailViewWidget::detailFontChanged(font);
    m_blockStatWidget->fontChanged(font);
    m_blocksummaryWidget->fontChanged(font);
}
