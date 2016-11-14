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

    int minimum_height;

    explicit AccordianBladeWidget(QWidget *parent = 0);
    ~AccordianBladeWidget();

    void addTransform();
    void addCheckBoxOption( QString name);
    void setMaxHeight( int height );
    void addColorPicker( QString name );
    void addTexturePicker( QString name );
    void setContentTitle( QString title );
    void addFloatValueSlider( QString name, float range_1 , float range_2 );

private slots:
    void on_toggle_clicked();

private:
    Ui::AccordianBladeWidget *ui;
};

#endif // ACCORDIANBLADEWIDGET_H
