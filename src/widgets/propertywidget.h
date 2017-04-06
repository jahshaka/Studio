#ifndef PROPERTYWIDGET_H
#define PROPERTYWIDGET_H

#include <QWidget>
#include "propertytype.h"

namespace Ui {
    class PropertyWidget;
}

class PropertyWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PropertyWidget(QWidget* parent = 0);
    ~PropertyWidget();

    void addProperty(const Property*);
    void setProperties(const QList<Property*>&);

private:
    Ui::PropertyWidget *ui;
};

#endif // PROPERTYWIDGET_H
