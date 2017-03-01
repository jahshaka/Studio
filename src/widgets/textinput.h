#ifndef TEXTINPUT_H
#define TEXTINPUT_H

#include <QWidget>

namespace Ui {
    class TextInput;
}

class TextInput : public QWidget
{
    Q_OBJECT

public:
    TextInput(QWidget* parent = 0);
    ~TextInput();

    void setText(const QString&);
    void setLabel(const QString&);
    void clearText();
    QString getText();

private slots:
    void onTextInputChanged(const QString&);

private:
    Ui::TextInput* ui;
};

#endif // TEXTINPUT_H
