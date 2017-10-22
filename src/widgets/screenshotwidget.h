#ifndef SCREENSHOTWIDGET_H
#define SCREENSHOTWIDGET_H

#include <QWidget>
#include <QDialog>

namespace Ui {
class ScreenshotWidget;
}

class ScreenshotWidget : public QDialog
{
    Q_OBJECT

public:
    explicit ScreenshotWidget(QWidget *parent = 0);
    ~ScreenshotWidget();

    void setImage(const QImage &value);
public slots:
    void saveImage();
    //void closeWindow();

private:
    Ui::ScreenshotWidget *ui;
    QImage image;
};

#endif // SCREENSHOTWIDGET_H
