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
    layout->setMargin(0);
    layout->addStretch();
    ui->content->setLayout(layout);
}

void PostProcessesWidget::setPostProcessMgr(const iris::PostProcessManagerPtr &postMan)
{
    clear();

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

void PostProcessesWidget::addPostProcess(QAction* action)
{
    if (action->text()=="Bloom") {
        auto widget = new PostProcessPropertyWidget();
        widget->expand();
        auto bloom = iris::BloomPostProcess::create();
        postProcesses.append(bloom);
        postProcessMgr->addPostProcess(bloom);
        widget->setPostProcess(bloom);
        widget->setPanelTitle("Bloom");

        ((QVBoxLayout*)ui->content->layout())->insertWidget(postProcesses.size()-1,widget);
    } else if (action->text()=="Radial Blur") {
        auto widget = new PostProcessPropertyWidget();
        widget->expand();
        auto bloom = iris::RadialBlurPostProcess::create();
        postProcesses.append(bloom);
        widget->setPostProcess(bloom);
        postProcessMgr->addPostProcess(bloom);
        widget->setPanelTitle("Radial Blur");

        ((QVBoxLayout*)ui->content->layout())->insertWidget(postProcesses.size()-1,widget);
    }
}
