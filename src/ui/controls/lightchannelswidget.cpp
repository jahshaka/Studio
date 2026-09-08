/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/controls/lightchannelswidget.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr quint32 kAll = 0xFFFFFFFFu;
constexpr quint32 kLow8 = 0x000000FFu;
}  // namespace

LightChannelsWidget::LightChannelsWidget(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(2);

    // TWO LINES, NOT ONE (properties-width lane, 2026-09-08). Eight check boxes,
    // a name and two buttons on ONE line cannot go below ~373 px: a check box's
    // minimum is its indicator plus its digit, and nothing about that shrinks.
    // The Properties dock has no horizontal scrollbar, so a row that cannot fit
    // is CLIPPED (ui.properties_width) — and this was the widest row left in the
    // Light and Mesh blades once the prose rows were fixed. The name and the two
    // buttons take the first line, the eight boxes the second: 284 px for the
    // whole panel, inside every width the column allows.
    auto *line = new QHBoxLayout;
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(2);
    line->addWidget(new QLabel(tr("Channels"), this));
    line->addStretch();

    auto *boxLine = new QHBoxLayout;
    boxLine->setContentsMargins(0, 0, 0, 0);
    // No spacing between the boxes: a check box carries its own padding, and the
    // eight of them are the widest thing left in these blades — the 14 px of
    // spacing was the difference between fitting the column's minimum width and
    // being clipped at it.
    boxLine->setSpacing(0);
    for (int i = 0; i < 8; ++i) {
        // The label is the CHANNEL INDEX the scripting verb speaks (0..7), not
        // a 1-based display number: a user who reads the panel and then writes
        // node.setLightMask(id, [3]) must get the box they ticked.
        auto *box = new QCheckBox(QString::number(i), this);
        box->setToolTip(tr("Lighting channel %1").arg(i));
        connect(box, &QCheckBox::toggled, this, [this, i](bool on) {
            const quint32 bit = 1u << i;
            emitMask(on ? (mMask | bit) : (mMask & ~bit));
        });
        mBoxes[i] = box;
        boxLine->addWidget(box);
    }
    boxLine->addStretch();
    mAll = new QPushButton(tr("All"), this);
    mNone = new QPushButton(tr("None"), this);
    for (QPushButton *b : {mAll, mNone}) {
        b->setMaximumWidth(48);
        line->addWidget(b);
    }
    // "All" restores the full 32-bit default, upper bits included: it is the
    // "turn this feature off for this node" button, and leaving a script's
    // upper bits behind would make it a lie.
    connect(mAll, &QPushButton::clicked, this, [this]() { emitMask(kAll); });
    connect(mNone, &QPushButton::clicked, this, [this]() { emitMask(0u); });
    outer->addLayout(line);
    outer->addLayout(boxLine);

    mDescription = new QLabel(this);
    mDescription->setWordWrap(true);
    mDescription->hide();
    outer->addWidget(mDescription);

    mNote = new QLabel(this);
    mNote->setWordWrap(true);
    mNote->setStyleSheet(QStringLiteral("color: #d08b3c;"));
    mNote->hide();
    outer->addWidget(mNote);

    refresh();
}

void LightChannelsWidget::setDescription(const QString &text)
{
    mDescription->setText(text);
    mDescription->setVisible(!text.isEmpty());
}

void LightChannelsWidget::setMask(quint32 mask)
{
    mMask = mask;
    refresh();
}

void LightChannelsWidget::emitMask(quint32 mask)
{
    if (mask == mMask) return;
    mMask = mask;
    refresh();
    emit maskChanged(mMask);
}

void LightChannelsWidget::refresh()
{
    for (int i = 0; i < 8; ++i) {
        QSignalBlocker block(mBoxes[i]);
        mBoxes[i]->setChecked((mMask & (1u << i)) != 0u);
    }
    // The honesty line. Two things this row cannot show: bits above 7 (a script
    // may set any of the 32), and the fact that "all channels" is the default
    // in which nothing is filtered at all.
    const quint32 upper = mMask & ~kLow8;
    if (mMask == kAll) {
        mNote->hide();
    } else if (upper) {
        int count = 0;
        for (int i = 8; i < 32; ++i)
            if (upper & (1u << i)) ++count;
        mNote->setText(tr("%1 channel(s) above 7 are also set (a script wrote them); "
                          "the boxes above keep them.")
                           .arg(count));
        mNote->show();
    } else if ((mMask & kLow8) == 0u) {
        mNote->setText(tr("No channels: this node is filtered out of direct lighting entirely."));
        mNote->show();
    } else {
        mNote->hide();
    }
}
