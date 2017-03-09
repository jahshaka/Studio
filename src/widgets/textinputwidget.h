#ifndef TEXTINPUTWIDGET_H
#define TEXTINPUTWIDGET_H

#include <QWidget>

namespace Ui {
    class TextInputWidget;
}

class TextInputWidget : public QWidget
{
    Q_OBJECT

public:
    TextInputWidget(QWidget* parent = 0);
    ~TextInputWidget();

    void setText(const QString&);
    void setLabel(const QString&);
    void clearText();
    QString getText();

private slots:
    void onTextInputChanged(const QString&);

private:
    Ui::TextInputWidget* ui;
};

#endif // TEXTINPUTWIDGET_H
