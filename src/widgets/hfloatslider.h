#ifndef HFLOATSLIDER_H
#define HFLOATSLIDER_H

#include <QWidget>

namespace Ui {
class HFloatSlider;
}

class HFloatSlider : public QWidget
{
    Q_OBJECT

public:
    explicit HFloatSlider(QWidget *parent = 0);
    ~HFloatSlider();
    Ui::HFloatSlider *ui;

protected slots:

};

#endif // HFLOATSLIDER_H
