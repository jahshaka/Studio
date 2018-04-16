#ifndef CREATEANIMATIONWIDGET_H
#define CREATEANIMATIONWIDGET_H

#include <QWidget>

namespace Ui {
class CreateAnimationWidget;
}

class QPushButton;

class CreateAnimationWidget : public QWidget
{
    Q_OBJECT



public:
    explicit CreateAnimationWidget(QWidget *parent = 0);
    ~CreateAnimationWidget();

    QPushButton* getCreateButton();

    void showButton();
    void hideButton();
    void setButtonText(QString text);

private:
    Ui::CreateAnimationWidget *ui;
};

#endif // CREATEANIMATIONWIDGET_H
