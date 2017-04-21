#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

#include "postprocesseswidget.h"
#include "ui_postprocesseswidget.h"
#include "propertywidgets/postprocesspropertywidget.h"

#include "../irisgl/src/graphics/postprocessmanager.h"
#include "../irisgl/src/postprocesses/bloompostprocess.h"
#include "../irisgl/src/postprocesses/radialblurpostprocess.h"

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

    if (ui->content->layout()) {
        delete ui->content->layout();
    }

    auto layout = new QVBoxLayout();
    layout->setMargin(0);
    layout->addStretch();
    ui->content->setLayout(layout);
}

PostProcessesWidget::~PostProcessesWidget()
{
    delete ui;
}

void PostProcessesWidget::setPostProcessMgr(const iris::PostProcessManagerPtr &value)
{
    postProcessMgr = value;
}

void PostProcessesWidget::addPostProcess(QAction* action)
{
    if (action->text()=="Bloom") {
        auto widget = new PostProcessPropertyWidget();
        widget->expand();
        auto bloom = iris::BloomPostProcess::create();
        postProcesses.append(bloom);
        postProcessMgr->addPostProcess(bloom);
        widget->setPostProcess(bloom);

        ui->content->layout()->addWidget(widget);
//        ((QVBoxLayout*)ui->content->layout())->insertWidget(0,widget);
    } else if (action->text()=="Radial Blur") {
        auto widget = new PostProcessPropertyWidget();
        widget->expand();
        auto bloom = iris::RadialBlurPostProcess::create();
        postProcesses.append(bloom);
        widget->setPostProcess(bloom);
        postProcessMgr->addPostProcess(bloom);

        ui->content->layout()->addWidget(widget);
//        ((QVBoxLayout*)ui->content->layout())->insertWidget(0,widget);
    }
}
