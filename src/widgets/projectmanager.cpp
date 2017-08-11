#include "projectmanager.h"
#include "ui_projectmanager.h"

#include <QStandardPaths>
#include "../constants.h"
#include "../irisgl/src/core/irisutils.h"
#include "../core/settingsmanager.h"

#include <QDebug>

ProjectManager::ProjectManager(QWidget *parent) : QWidget(parent), ui(new Ui::ProjectManager)
{
    ui->setupUi(this);

    ui->listWidget->setContextMenuPolicy(Qt::CustomContextMenu);

    ui->listWidget->setFlow(QListView::LeftToRight);
    ui->listWidget->setViewMode(QListWidget::IconMode);
    ui->listWidget->setSpacing(24);
    ui->listWidget->setIconSize(QSize(212, 152));
    ui->listWidget->setIconSize(QSize(196, 152));
    ui->listWidget->setResizeMode(QListWidget::Adjust);
    ui->listWidget->setMovement(QListView::Static);
    ui->listWidget->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->listWidget->setSelectionMode(QAbstractItemView::SingleSelection);

    settings = SettingsManager::getDefaultManager();

    auto path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + Constants::PROJECT_FOLDER;
    auto projectFolder = settings->getValue("default_directory", path).toString();

    QDir dir(projectFolder);
    QFileInfoList files = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Dirs);

    foreach (const QFileInfo &file, files) {
        auto item = new QListWidgetItem();
        item->setToolTip(file.absoluteFilePath());
        item->setData(Qt::DisplayRole, file.baseName());
        item->setData(Qt::UserRole, file.absoluteFilePath() + "/" + file.baseName() + Constants::PROJ_EXT);
        if (QFile::exists(file.absoluteFilePath() + "/Metadata/preview.png")) {
            item->setIcon(QIcon(file.absoluteFilePath() + "/Metadata/preview.png"));
        } else {
            item->setIcon(QIcon(":/app/images/no_preview.png"));
        }
        ui->listWidget->addItem(item);
    }

    connect(ui->deleteProject,  SIGNAL(pressed()), SLOT(deleteProject()));
//    connect(ui->listWidget,     SIGNAL(itemDoubleClicked(QListWidgetItem*)),
//            this,               SLOT(openRecentProject(QListWidgetItem*)));
    connect(ui->listWidget,     SIGNAL(customContextMenuRequested(const QPoint&)),
            this,               SLOT(listWidgetCustomContextMenu(const QPoint&)));
}

void ProjectManager::listWidgetCustomContextMenu(const QPoint &pos)
{
    QModelIndex index = ui->listWidget->indexAt(pos);

//    QMenu menu;
//    QAction *action;

//    if (index.isValid()) {
//        currentItem = ui->listWidget->itemAt(pos);
////        assetItem.selectedPath = item->data(Qt::UserRole).toString();

//        action = new QAction(QIcon(), "Remove from recent list", this);
//        connect(action, SIGNAL(triggered()), this, SLOT(removeFromList()));
//        menu.addAction(action);

//        if (!isMainWindowActive) {
//            action = new QAction(QIcon(), "Delete project", this);
//            connect(action, SIGNAL(triggered()), this, SLOT(deleteProject()));
//            menu.addAction(action);
//        }
//    }

//    menu.exec(ui->listWidget->mapToGlobal(pos));
}

void ProjectManager::removeFromList()
{
//    auto selectedInfo = QFileInfo(currentItem->data(Qt::UserRole).toString());
//    delete ui->listWidget->takeItem(ui->listWidget->row(currentItem));
//    settings->removeRecentlyOpenedEntry(selectedInfo.absoluteFilePath());

//    if (!ui->listWidget->count()) {
//        ui->label->show();
//        ui->listWidget->hide();
//    }
}

void ProjectManager::deleteProject()
{
//    auto selectedInfo = QFileInfo(currentItem->data(Qt::UserRole).toString());

//    QDir dir(selectedInfo.absolutePath());
//    if (dir.removeRecursively()) {
//        removeFromList();
//    }
}

ProjectManager::~ProjectManager()
{
    delete ui;
}
