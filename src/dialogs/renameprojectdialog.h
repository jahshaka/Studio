#ifndef RENAMEPROJECTDIALOG_H
#define RENAMEPROJECTDIALOG_H

#include <QDialog>

namespace Ui {
    class RenameProjectDialog;
}

class RenameProjectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RenameProjectDialog(QDialog *parent = Q_NULLPTR);
    ~RenameProjectDialog();

public slots:
    void newText();

signals:
    void newTextEmit(const QString &);

private:
    Ui::RenameProjectDialog *ui;

protected:
	void changeEvent(QEvent* event) override;
};

#endif // RENAMEPROJECTDIALOG_H
