#ifndef COLORWIDGET_HPP
#define COLORWIDGET_HPP

#include <QWidget>

namespace Ui {
    class ColorWidget;
}

class ColorWidget : public QWidget
{
    Q_OBJECT

public:
    ColorWidget(QWidget* parent = nullptr);

    void setLabel(QString);
    void setColor(QColor);

private:
    Ui::ColorWidget* ui;
};

#endif // COLORWIDGET_HPP
