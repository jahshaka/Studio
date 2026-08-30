/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/publish/publishmodule.h"

#include <QLabel>
#include <QVBoxLayout>

QWidget *PublishModule::createPage()
{
    // The stub page that used to be built inline in MainWindow::setupDesktop.
    auto *page = new QWidget(host.shellWidget);
    page->setObjectName("publishView");
    page->setStyleSheet("#publishView { background: #1e1e1e; }");

    auto *vl = new QVBoxLayout(page);
    auto *title = new QLabel(QStringLiteral("Publish"));
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size: 32px; font-weight: 500; color: rgba(255, 255, 255, 0.92); background: transparent;");
    auto *subtitle = new QLabel(QStringLiteral("Coming soon: publish your scene to the web, Unreal, and more."));
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet("font-size: 15px; color: rgba(255, 255, 255, 0.55); background: transparent;");
    vl->addStretch();
    vl->addWidget(title);
    vl->addSpacing(8);
    vl->addWidget(subtitle);
    vl->addStretch();
    return page;
}
