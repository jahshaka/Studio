#include <QFile>
#include <QFontDatabase>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMessageBox>

#include "src/irisgl/src/core/irisutils.h"
#include "../mainwindow.h"
#include "../core/settingsmanager.h"

#include "newprojectdialog.h"

#include "projectdialog.h"
#include "ui_projectdialog.h"

#include "../core/project.h"
#include "../core/database/database.h"
#include "../globals.h"
#include "../constants.h"

#include <QDebug>
#include <QFileDialog>

#include "../core/guidmanager.h"

ProjectDialog::ProjectDialog(QDialog *parent) : QDialog(parent), ui(new Ui::ProjectDialog)
{
    ui->setupUi(this);

    this->setWindowTitle("Jahshaka VR");

    QFile fontFile(IrisUtils::getAbsoluteAssetPath("app/fonts/OpenSans-Bold.ttf"));
    if (fontFile.exists()) {
        fontFile.open(QIODevice::ReadOnly);
        QFontDatabase::addApplicationFontFromData(fontFile.readAll());
        QApplication::setFont(QFont("Open Sans", 9));
    }

    ui->listWidget->setViewMode(QListWidget::IconMode);
    ui->listWidget->setIconSize(QSize(256, 256));
    ui->listWidget->setResizeMode(QListWidget::Adjust);
    ui->listWidget->setMovement(QListView::Static);
    ui->listWidget->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->listWidget->setSelectionMode(QAbstractItemView::SingleSelection);

    ui->demoList->setViewMode(QListWidget::IconMode);
    ui->demoList->setIconSize(QSize(256, 256));
    ui->demoList->setResizeMode(QListWidget::Adjust);
    ui->demoList->setMovement(QListView::Static);
    ui->demoList->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->demoList->setSelectionMode(QAbstractItemView::SingleSelection);

    connect(ui->newProject,     SIGNAL(pressed()), SLOT(newScene()));
    connect(ui->openProject,    SIGNAL(pressed()), SLOT(openProject()));
    connect(ui->listWidget,     SIGNAL(itemDoubleClicked(QListWidgetItem*)),
            this,               SLOT(openRecentProject(QListWidgetItem*)));
    connect(ui->demoList,       SIGNAL(itemDoubleClicked(QListWidgetItem*)),
            this,               SLOT(openSampleProject(QListWidgetItem*)));

    settings = SettingsManager::getDefaultManager();

    for (auto recentItem : settings->getRecentlyOpenedScenes()) {
        auto item = new QListWidgetItem();
        item->setData(Qt::UserRole, recentItem);
        item->setToolTip(recentItem);
        auto fn = QFileInfo(recentItem);
        if (QFile::exists(fn.absolutePath() + "/Metadata/preview.png")) {
            item->setIcon(QIcon(fn.absolutePath() + "/Metadata/preview.png"));
        } else {
            item->setIcon(QIcon(":/app/images/no_preview.png"));
        }
        item->setText(fn.baseName());
        ui->listWidget->addItem(item);
    }

    QDir dir(IrisUtils::getAbsoluteAssetPath(Constants::SAMPLES_FOLDER));
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
        ui->demoList->addItem(item);
    }
}

ProjectDialog::~ProjectDialog()
{
    delete ui;
}

void ProjectDialog::newScene()
{
    NewProjectDialog dialog;

    dialog.exec();

    auto projectName = dialog.getProjectInfo().projectName;
    auto projectPath = dialog.getProjectInfo().projectPath;

    if (!projectName.isEmpty() || !projectName.isNull()) {
        auto fullProjectPath = QDir(projectPath).filePath(projectName);
        auto slnName = QDir(fullProjectPath).filePath(projectName + Constants::PROJ_EXT);

        Globals::project->setFilePath(slnName);
        Globals::project->setProjectPath(fullProjectPath);

        // make a dir and the default subfolders
        QDir projectDir(fullProjectPath);
        if (!projectDir.exists()) projectDir.mkpath(".");

        for (auto folder : Constants::PROJECT_DIRS) {
            QDir dir(QDir(fullProjectPath).filePath(folder));
            dir.mkpath(".");
        }

        window = new MainWindow;
        window->showMaximized();
        window->newProject(projectName, fullProjectPath);
        settings->addRecentlyOpenedScene(slnName);
        settings->setValue("last_wd", projectPath);

        this->close();
    }
}

void ProjectDialog::openProject()
{
    auto projectFile = QFileInfo(loadProjectDelegate());
    auto projectPath = projectFile.absolutePath();

    if (!projectPath.isEmpty()) {
        Globals::project->setProjectPath(projectPath);

        window = new MainWindow;
        window->showMaximized();
        window->openProject(projectFile.absoluteFilePath());

        settings->addRecentlyOpenedScene(projectFile.absoluteFilePath());
        this->close();
    }

    else {
        // QMessageBox msgBox;
        // msgBox.setStyleSheet("QLabel { width: 128px; }");
        // msgBox.setIcon(QMessageBox::Warning);
        // msgBox.setText("Unable to locate project!");
        // msgBox.setInformativeText("You did not select a folder or the path is invalid");
        // msgBox.setStandardButtons(QMessageBox::Ok);
        // msgBox.exec();
    }
}

QString ProjectDialog::loadProjectDelegate()
{
    auto projectFileName = QFileDialog::getOpenFileName(this, "Select Project File",
                                                        nullptr, "Jahshaka Project File (*.project)");
    return projectFileName;
}

void ProjectDialog::openRecentProject(QListWidgetItem *item)
{
    auto projectFile = QFileInfo(item->data(Qt::UserRole).toString());
    auto projectPath = projectFile.absolutePath();
    Globals::project->setProjectPath(projectPath);

    window = new MainWindow;
    window->showMaximized();

    window->openProject(projectFile.absoluteFilePath());

    this->close();
}

bool ProjectDialog::copyDirectoryFiles(const QString &fromDir, const QString &toDir, bool coverFileIfExist)
{
    QDir sourceDir(fromDir);
    QDir targetDir(toDir);
    if(!targetDir.exists()){    /* if directory don't exists, build it */
        if(!targetDir.mkdir(targetDir.absolutePath()))
            return false;
    }

    QFileInfoList fileInfoList = sourceDir.entryInfoList();
    foreach(QFileInfo fileInfo, fileInfoList){
        if(fileInfo.fileName() == "." || fileInfo.fileName() == "..")
            continue;

        if(fileInfo.isDir()){    /* if it is directory, copy recursively*/
            if(!copyDirectoryFiles(fileInfo.filePath(),
                targetDir.filePath(fileInfo.fileName()),
                coverFileIfExist))
                return false;
        }
        else{            /* if coverFileIfExist == true, remove old file first */
            if(coverFileIfExist && targetDir.exists(fileInfo.fileName())){
                targetDir.remove(fileInfo.fileName());
            }

            // files copy
            if(!QFile::copy(fileInfo.filePath(),
                targetDir.filePath(fileInfo.fileName()))){
                    return false;
            }
        }
    }
    return true;
}

void ProjectDialog::openSampleProject(QListWidgetItem *item)
{
    auto projectFolder = QFileDialog::getExistingDirectory(nullptr, "Select Copy Directory");

    if (!projectFolder.isEmpty()) {
        auto projectFile = QFileInfo(item->data(Qt::UserRole).toString());

        QString dest = QDir(projectFolder).filePath(projectFile.baseName());
        if (this->copyDirectoryFiles(projectFile.absolutePath(), dest, true)) {

            auto newProjectFile = QFileInfo(dest);
            auto projectPath = QDir(newProjectFile.absolutePath()).filePath(projectFile.baseName());
            Globals::project->setProjectPath(projectPath);

            auto sln = QDir(projectPath).filePath(projectFile.fileName());

            window = new MainWindow;
            window->showMaximized();
            window->openProject(sln);

            settings->addRecentlyOpenedScene(sln);
        }
    }

    this->close();
}

SettingsManager *ProjectDialog::getSettingsManager()
{
    return settings;
}

bool ProjectDialog::eventFilter(QObject *watched, QEvent *event)
{
    return QObject::eventFilter(watched, event);
}

