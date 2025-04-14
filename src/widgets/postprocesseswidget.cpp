/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

#include "postprocesseswidget.h"
#include "ui_postprocesseswidget.h"
#include "propertywidgets/postprocesspropertywidget.h"

#include "../irisgl/src/graphics/postprocess.h"
#include "../irisgl/src/graphics/postprocessmanager.h"
#include "../irisgl/src/postprocesses/bloompostprocess.h"
#include "../irisgl/src/postprocesses/radialblurpostprocess.h"
#include "../irisgl/src/postprocesses/ssaopostprocess.h"
#include "../irisgl/src/postprocesses/greyscalepostprocess.h"
#include "../irisgl/src/postprocesses/coloroverlaypostprocess.h"

#include <QDebug>

PostProcessesWidget::PostProcessesWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PostProcessesWidget)
{
    ui->setupUi(this);

    QMenu* processList = new QMenu();
    processList->addAction("Bloom");
    processList->addAction("Radial Blur");
    processList->addAction("GreyScale");
    processList->addAction("Color Overlay");
    processList->addAction("SSAO");
    ui->addButton->setMenu(processList);
    ui->addButton->setPopupMode(QToolButton::InstantPopup);

    connect(processList, SIGNAL(triggered(QAction*)), this, SLOT(addPostProcess(QAction*)));

    clear();
}

PostProcessesWidget::~PostProcessesWidget()
{
    delete ui;
}

void PostProcessesWidget::clearLayout(QLayout *layout)
{
    while (auto item = layout->takeAt(0)) {
        if (auto widget = item->widget()) widget->deleteLater();

        if (auto childLayout = item->layout()) {
            this->clearLayout(childLayout);
        }

        delete item;
    }

    delete layout;
}

void PostProcessesWidget::clear()
{
    if (ui->content->layout()) {
        clearLayout(ui->content->layout());
    }

    auto layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch();
    ui->content->setLayout(layout);
}

void PostProcessesWidget::setPostProcessMgr(const iris::PostProcessManagerPtr &postMan)
{
    clear();
	if (!!postMan) {
		postProcessMgr = postMan;

		int i = 0;
		for (auto process : postMan->getPostProcesses()) {
			auto widget = new PostProcessPropertyWidget();
			widget->expand();

			postProcesses.append(process);
			widget->setPostProcess(process);
			widget->setPanelTitle(process->getDisplayName());

			((QVBoxLayout*)ui->content->layout())->insertWidget(i, widget);

			i++;
		}
	}
}

void PostProcessesWidget::addPostProcess(QAction* action)
{
    iris::PostProcessPtr process;
	/*
    if (action->text()=="Bloom") {
        process = iris::BloomPostProcess::create();
    } else if (action->text()=="Radial Blur") {
        process = iris::RadialBlurPostProcess::create();
    } else if (action->text()=="GreyScale") {
        process = iris::GreyscalePostProcess::create();
    } else if (action->text()=="Color Overlay") {
        process = iris::ColorOverlayPostProcess::create();
    } else if (action->text()=="SSAO") {
        process = iris::SSAOPostProcess::create();
    }
	*/
    if (!!process) {
        auto widget = new PostProcessPropertyWidget();
        widget->expand();

        postProcesses.append(process);
        postProcessMgr->addPostProcess(process);
        widget->setPostProcess(process);
        widget->setPanelTitle(process->getDisplayName());

        ((QVBoxLayout*)ui->content->layout())->insertWidget(postProcesses.size()-1,widget);
    }
}
