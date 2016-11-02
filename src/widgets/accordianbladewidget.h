#ifndef ACCORDIANBLADEWIDGET_H
#define ACCORDIANBLADEWIDGET_H

#include <QWidget>

namespace Ui {
class AccordianBladeWidget;
}

class AccordianBladeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AccordianBladeWidget(QWidget *parent = 0);
    ~AccordianBladeWidget();
    void addColorPicker( QString name );
    void setContentTitle( QString title );
    void addFloatValueSlider( QString name, float range_1 , float range_2 );
    //void addTexturePicker( QString name );

private slots:
    void on_toggle_clicked();

private:
    Ui::AccordianBladeWidget *ui;
};

#endif // ACCORDIANBLADEWIDGET_H
