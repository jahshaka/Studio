#ifndef PROJECTDIALOG_H
#define PROJECTDIALOG_H

#include <QDialog>
#include <QListWidgetItem>

namespace Ui {
    class ProjectDialog;
}

class MainWindow;
class SettingsManager;

class ProjectDialog : public QDialog
{
    Q_OBJECT

public:
    ProjectDialog(QDialog *parent = nullptr);
    ~ProjectDialog();

    MainWindow *window;

    void setSettingsManager(SettingsManager* settings);
    QString loadProjectDelegate();
    SettingsManager* getSettingsManager();
    SettingsManager* settings;

protected:
    bool eventFilter(QObject *watched, QEvent *event);

protected slots:
    void newScene();
    void openProject();
    void openRecentProject(QListWidgetItem*);

private:
    Ui::ProjectDialog *ui;
};

#endif // PROJECTDIALOG_H
