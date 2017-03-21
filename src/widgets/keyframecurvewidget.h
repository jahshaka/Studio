#ifndef KEYFRAMECURVEWIDGET_H
#define KEYFRAMECURVEWIDGET_H

#include <QWidget>

namespace Ui {
class KeyFrameCurveWidget;
}

class KeyFrameCurveWidget : public QWidget
{
    Q_OBJECT

public:
    explicit KeyFrameCurveWidget(QWidget *parent = 0);
    ~KeyFrameCurveWidget();

private:
    Ui::KeyFrameCurveWidget *ui;
};

#endif // KEYFRAMECURVEWIDGET_H
