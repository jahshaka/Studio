#ifndef GETNAMEDIALOG_H
#define GETNAMEDIALOG_H

#include <QDialog>

namespace Ui {
class GetNameDialog;
}

class GetNameDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GetNameDialog(QWidget *parent = 0);
    ~GetNameDialog();

    void setName(QString name);
    QString getName();

private:
    Ui::GetNameDialog *ui;
};

#endif // GETNAMEDIALOG_H
