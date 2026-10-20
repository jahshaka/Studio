/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "shell/streamingindicator.h"

#include "ui/style/themeroles.h"

#include <QAction>
#include <QLabel>
#include <QStringList>
#include <QTimer>
#include <QToolBar>

StreamingIndicator::StreamingIndicator(QToolBar *bar, Source source) : QObject(bar), mSource(std::move(source))
{
    mLabel = new QLabel;
    mLabel->setObjectName(QStringLiteral("shaderCompileIndicator"));
    ThemeRoles::setToolbarText(mLabel);   // the theme's window text on the toolbar's band, never its button text
    mAction = bar->addWidget(mLabel);
    mAction->setObjectName(QStringLiteral("actionShaderCompiles"));
    mAction->setVisible(false);
    if (!mSource) return;
    mPoll = new QTimer(this);
    mPoll->setInterval(250);
    connect(mPoll, &QTimer::timeout, this, [this]() { refresh(); });
    mPoll->start();
}

QString StreamingIndicator::compose(const Work &w)
{
    QStringList parts;
    if (w.shaders) parts << tr("Compiling shaders (%1)").arg(w.shaders);
    if (w.textures) parts << tr("Loading textures (%1)").arg(w.textures);
    if (w.gi) parts << tr("Lighting the world");
    return parts.join(QStringLiteral("  ·  "));
}

void StreamingIndicator::refresh()
{
    const Work w = mSource ? mSource() : Work();
    if (w.any()) {
        const QString text = compose(w);
        if (mLabel->text() != text) {
            mLabel->setText(text);
            mAction->setToolTip(text);
        }
    }
    if (mAction->isVisible() != w.any()) mAction->setVisible(w.any());
}
