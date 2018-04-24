#ifndef NEWPROJECTDIALOG_H
#define NEWPROJECTDIALOG_H

#include <QDialog>

namespace Ui {
    class NewProjectDialog;
}

struct ProjectInfo {
    QString projectName;
    QString projectPath;
};

class SettingsManager;

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
    void confirmProjectCreation();

private:
    Ui::NewProjectDialog *ui;
    QString projectName,
            projectPath;
    SettingsManager *settingsManager;
    QString lastValue;

protected:
    void changeEvent(QEvent* event) override;
};

#endif // NEWPROJECTDIALOG_H
