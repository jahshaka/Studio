#ifndef PROGRESSDIALOG_H
#define PROGRESSDIALOG_H

#include <QDialog>

namespace Ui {
    class ProgressDialog;
}

class ProgressDialog : public QDialog
{
    Q_OBJECT

public:
    ProgressDialog(QDialog *parent = nullptr);
    ~ProgressDialog();

public slots:
    void setLabelText(const QString&);
    void setRange(int, int);
    void setValue(int);
    void reset();

private:
    Ui::ProgressDialog *ui;
};

#endif // PROGRESSDIALOG_H
