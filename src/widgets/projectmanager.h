#ifndef PROJECTMANAGER_H
#define PROJECTMANAGER_H

namespace Ui {
    class ProjectManager;
}

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

private:
    Ui::ProjectManager *ui;
};

#endif // PROJECTMANAGER_H
