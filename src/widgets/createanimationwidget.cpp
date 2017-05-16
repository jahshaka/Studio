#include "createanimationwidget.h"
#include "ui_createanimationwidget.h"

CreateAnimationWidget::CreateAnimationWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::CreateAnimationWidget)
{
    ui->setupUi(this);
}

CreateAnimationWidget::~CreateAnimationWidget()
{
    delete ui;
}

QPushButton *CreateAnimationWidget::getCreateButton()
{
    return ui->newAnimButton;
}

void CreateAnimationWidget::showButton()
{
    ui->newAnimButton->show();
}

void CreateAnimationWidget::hideButton()
{
    ui->newAnimButton->hide();
}

void CreateAnimationWidget::setButtonText(QString text)
{
    ui->newAnimButton->setText(text);
}
