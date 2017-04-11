#ifndef PROJECTDIALOG_H
#define PROJECTDIALOG_H

#include <QDialog>

namespace Ui {
    class ProjectDialog;
}

class MainWindow;

class ProjectDialog : public QDialog
{
    Q_OBJECT

public:
    ProjectDialog(QDialog *parent = nullptr);
    ~ProjectDialog();

    MainWindow *window;

protected slots:
    void newScene();
    void openProject();

private:
    Ui::ProjectDialog *ui;
};

#endif // PROJECTDIALOG_H
