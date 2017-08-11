#ifndef PROJECTMANAGER_H
#define PROJECTMANAGER_H

namespace Ui {
    class ProjectManager;
}

class SettingsManager;

#include <QListWidget>
#include <QTreeWidgetItem>
#include <QWidget>
#include <QFileDialog>

class ProjectManager : public QWidget
{
    Q_OBJECT

public:
    ProjectManager(QWidget *parent = nullptr);
    ~ProjectManager();

protected slots:
    void listWidgetCustomContextMenu(const QPoint&);
    void removeFromList();
    void deleteProject();

private:
    Ui::ProjectManager *ui;
    SettingsManager* settings;
};

#endif // PROJECTMANAGER_H
