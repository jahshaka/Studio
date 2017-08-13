#include "screenshotwidget.h"
#include "ui_screenshotwidget.h"

ScreenshotWidget::ScreenshotWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ScreenshotWidget)
{
    ui->setupUi(this);
}

ScreenshotWidget::~ScreenshotWidget()
{
    delete ui;
}
