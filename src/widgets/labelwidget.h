#ifndef LABELWIDGET_H
#define LABELWIDGET_H

#include <QWidget>

namespace Ui {
    class LabelWidget;
}

class LabelWidget : public QWidget
{
    Q_OBJECT

public:
    LabelWidget(QWidget* parent = 0);
    ~LabelWidget();

    void setText(const QString&);
    void setLabel(const QString&);
    void clearText();

private slots:
    void onTextInputChanged(const QString&);

private:
    Ui::LabelWidget* ui;
};


#endif // LABELWIDGET_H
