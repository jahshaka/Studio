#include "screenshotwidget.h"
#include "ui_screenshotwidget.h"
#include <QFileDialog>

ScreenshotWidget::ScreenshotWidget(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ScreenshotWidget)
{
    ui->setupUi(this);

    this->setWindowTitle("Screenshot");
    ui->label->setScaledContents(true);

    connect(ui->saveButton, SIGNAL(pressed()), this, SLOT(saveImage()));
    connect(ui->closeButton, SIGNAL(pressed()), this, SLOT(close()));
}

ScreenshotWidget::~ScreenshotWidget()
{
    delete ui;
}

void ScreenshotWidget::setImage(const QImage &value)
{
    image = value;
    ui->label->setPixmap(QPixmap::fromImage(image));
}

void ScreenshotWidget::saveImage()
{
    //QFileDialog::getSaveFileName(this, "Save Scene", "", "Jashaka Scene (*.jah)");
    auto filePath = QFileDialog::getSaveFileName(this, "Choose image path","screenshot.png","Supported Image Formats (*.jpg, *.png)");

    if (filePath.isEmpty() || filePath.isNull()) return;
    image.save(filePath);
}
