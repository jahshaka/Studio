#ifndef NEWPROJECTDIALOG_H
#define NEWPROJECTDIALOG_H

#include <QDialog>

namespace Ui {
    class NewProjectDialog;
};

struct ProjectInfo {
    QString projectName;
    QString projectPath;
};

class NewProjectDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NewProjectDialog(QDialog *parent = 0);
    ~NewProjectDialog();

    ProjectInfo getProjectInfo();

protected slots:
    void setProjectPath();
    void createNewProject();

    void confirm();

private:
    Ui::NewProjectDialog *ui;
    QString projectName,
            projectPath;
};

#endif // NEWPROJECTDIALOG_H
