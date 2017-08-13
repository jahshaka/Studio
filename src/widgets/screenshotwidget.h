#ifndef SCREENSHOTWIDGET_H
#define SCREENSHOTWIDGET_H

#include <QWidget>

namespace Ui {
class ScreenshotWidget;
}

class ScreenshotWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ScreenshotWidget(QWidget *parent = 0);
    ~ScreenshotWidget();

private:
    Ui::ScreenshotWidget *ui;
};

#endif // SCREENSHOTWIDGET_H
