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

    connect(ui->newProject,     SIGNAL(pressed()), SLOT(newScene()));
    connect(ui->openProject,    SIGNAL(pressed()), SLOT(openProject()));
    connect(ui->listWidget,     SIGNAL(itemDoubleClicked(QListWidgetItem*)),
            this,               SLOT(openRecentProject(QListWidgetItem*)));

    settings = SettingsManager::getDefaultManager();
    ui->listWidget->addItems(settings->getRecentlyOpenedScenes());

//    db = new Database();
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
        auto slnName = projectName + Constants::PROJ_EXT;
        auto jahFile = QDir(fullProjectPath + "/Scenes").filePath(projectName + Constants::JAH_EXT);

        Globals::project->setFilePath(jahFile);
        Globals::project->setProjectPath(fullProjectPath);

        // make a dir and the default subfolders
        QDir projectDir(fullProjectPath);
        if (!projectDir.exists()) projectDir.mkpath(".");

        for (auto folder : Constants::PROJECT_DIRS) {
            QDir dir(QDir(fullProjectPath).filePath(folder));
            dir.mkpath(".");
        }

        // copy default scene to new project and open that as the new project
        QFile::copy(IrisUtils::getAbsoluteAssetPath("scenes/startup/tile.png"),
                    QDir(fullProjectPath + "/Textures").filePath("tile.png"));

        QFile::copy(IrisUtils::getAbsoluteAssetPath("scenes/startup/ground.obj"),
                    QDir(fullProjectPath + "/Models").filePath("ground.obj"));

        QFile::copy(IrisUtils::getAbsoluteAssetPath("scenes/startup/startup.jah"), jahFile);

        QFile slnFile(QDir(Globals::project->getProjectFolder()).filePath(slnName));
        slnFile.open(QIODevice::WriteOnly | QIODevice::Truncate);

        QJsonObject projectObj;
        QJsonObject activeObj;
        activeObj["path"] = "Scenes/" + projectName + Constants::JAH_EXT;
        activeObj["thumbnail"] = "";
        activeObj["version"] = Constants::CONTENT_VERSION;
        projectObj["activeProject"] = activeObj;
        QJsonDocument projectSln(projectObj);
        slnFile.write(projectSln.toJson());
        slnFile.close();

        window = new MainWindow;
        window->showMaximized();
        // window->newProject(projectName, projectPath);
        window->openProject(jahFile);
        settings->addRecentlyOpenedScene(fullProjectPath);

        this->close();
    }
}

void ProjectDialog::openProject()
{
    auto projectPath = loadProjectDelegate();

    if (!projectPath.isEmpty()) {
        auto projectDir = projectPath.split('/').last();
        Globals::project->setProjectPath(projectPath);
        auto projectName = QDir(projectPath).filePath(projectDir + Constants::PROJ_EXT);

        // TODO - move this to a utils class or something
        QFile file(projectName);
        file.open(QIODevice::ReadOnly);
        auto data = file.readAll();
        file.close();
        auto projectObject = QJsonDocument::fromJson(data).object();
        auto activeObject = projectObject["activeProject"].toObject();

        window = new MainWindow;
        window->showMaximized();
        window->openProject(QDir(projectPath).filePath(activeObject["path"].toString()));

        settings->addRecentlyOpenedScene(projectPath);
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
    auto projectFoler = QFileDialog::getExistingDirectory(this, "Select Project Folder");
    return projectFoler;
}

void ProjectDialog::openRecentProject(QListWidgetItem *item)
{
    Globals::project->setProjectPath(item->text());

    window = new MainWindow;
    window->showMaximized();
    window->openProject(item->text() + "/Scenes/" + item->text().split('/').last() + Constants::JAH_EXT);

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

