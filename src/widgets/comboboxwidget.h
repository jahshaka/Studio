#ifndef COMBOBOXWIDGET_H
#define COMBOBOXWIDGET_H

#include <QWidget>

namespace Ui {
    class ComboBoxWidget;
}

class ComboBoxWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ComboBoxWidget(QWidget* parent = 0);
    ~ComboBoxWidget();

    void setLabel(const QString&);
    void addItem(const QString&);

private slots:
    void onDropDownTextChanged(const QString&);

private:
    Ui::ComboBoxWidget* ui;
};

#endif // COMBOBOXWIDGET_H
