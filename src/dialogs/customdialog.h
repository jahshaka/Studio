#ifndef CUSTOMDIALOG_H
#define CUSTOMDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

class CustomDialog : public QDialog
{
public:
    CustomDialog(Qt::Orientation ori = Qt::Horizontal);
    QWidget *holder; // body of the widget
    QWidget *buttonWidget;
	QVBoxLayout *holderLayout;
	QVBoxLayout *mainLayout;
	QLineEdit *lineEdit;
    QPushButton *okBtn;
    QPushButton *cancelBtn;
    QLayout *buttonHolder;

    virtual void addConfirmButton(QString text = "ok");
    virtual void addCancelButton(QString text = "cancel");
	void addMessage(QString msg);
	void addTitle(QString title);
    void addConfirmAndCancelButtons(QString confirmButtonText = "ok", QString cancelButtonText = "cancel");
	void setButtonOrientations(Qt::Orientation orientation);
	void setHolderWidth(int width);
	void sendAcceptSignal(bool accept);

    void insertWidget(QWidget *widget, int index = -1);
	int index = 1;// level at which new widgets are inserted

private:
    void configureConnections();
};

#endif // CUSTOMDIALOG_H
