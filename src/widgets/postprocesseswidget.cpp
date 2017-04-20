#include <QMenu>
#include <QVBoxLayout>

#include "postprocesseswidget.h"
#include "ui_postprocesseswidget.h"
#include "propertywidgets/postprocesspropertywidget.h"


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

    connect(processList, SIGNAL(triggered(QAction*)), this, SLOT(addPostProcess(QAction*)));

    if (ui->content->layout()) {
        delete ui->content->layout();
    }

    auto layout = new QVBoxLayout();
    layout->setMargin(0);
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
    if (action->text()=="Bloom")
    {
        auto widget = new PostProcessPropertyWidget();
        ui->content->layout()->addWidget(widget);
    }
}
